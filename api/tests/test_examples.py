"""Console lifecycle, BUSY wire handling and readback; no hardware IO."""

import importlib.util
from pathlib import Path
import struct
import threading
import time
import unittest
from unittest.mock import Mock

from tunnel_sdk import BusyError, Modbus, Mqtt, ProtocolError, UnknownOutcome, require_accepted
from tunnel_sdk.common import crc16
from test_device import CASES

spec = importlib.util.spec_from_file_location(
    "device_console", Path(__file__).resolve().parents[1] / "example/device_console.py"
)
example = importlib.util.module_from_spec(spec)
spec.loader.exec_module(example)


class FakeDevice:
    def __init__(self):
        self.owned = False
        self.events = []
        self.sequence = 0
        self.powers = [20, 21, 22, 23]
        self.mask, self.profile, self.gen2x = 5, 0, 4123
        self.busy = False
        self.pending_until = 0
        self.mismatch = False

    def has_control(self):
        return self.owned

    def get_status(self):
        return dict(epc_count=7, total_reads=20, state=9)

    def get_reader_result(self, sequence, *, expected_command):
        if time.monotonic() < self.pending_until:
            return None
        payload = {
            0x94: ([1, 1, 1, 1] if self.mismatch else self.powers),
            0x21: [0] * 8 + [self.mask],
            0x7F: self.profile_reply,
            0x91: [17],
        }.get(expected_command, [])
        return dict(sequence=sequence, data=payload, raw_reply=[expected_command, 0] + payload)

    @property
    def profile_reply(self):
        return [self.gen2x >> 8, self.gen2x & 255] if self.extended else [self.profile]

    extended = False

    def __getattr__(self, name):
        def call(*args, **kwargs):
            self.events.append((name, args, kwargs))
            if self.busy and name.startswith("set_"):
                return dict(accepted=False, result=2, sequence=self.sequence)
            if name == "claim":
                self.owned = True
            if name in ("release", "stop"):
                self.owned = False
            if name == "set_antenna_power":
                self.powers = [kwargs[f"ant{i}_dbm"] for i in range(1, 5)]
            if name == "set_antenna_mask":
                self.mask = kwargs["mask"]
            if name == "set_rf_profile":
                self.profile = kwargs["encoded"] & 63
            if name == "set_gen2x":
                self.gen2x = kwargs["value"]
            if name in ("get_gen2x", "set_gen2x"):
                self.extended = True
            if name in ("get_rf_profile", "set_rf_profile"):
                self.extended = False
            self.sequence += 1
            return dict(accepted=True, result=0, sequence=self.sequence)

        return call


class ExampleTests(unittest.TestCase):
    def test_all_19_commands_call_public_helpers(self):
        expected = {case[0] for case in CASES}
        self.assertEqual(expected, {spec[1] for spec in example.COMMANDS.values()})
        d = FakeDevice()
        c = example.Console(d)
        try:
            c.run("claim")
            for method, positional, keyword, op, words in CASES:
                name = next(n for n, s in example.COMMANDS.items() if s[1] == method)
                values = list(positional) + list(keyword.values())
                line = name + " " + " ".join(map(str, values))
                parsed, params = example.parse_command(line)
                before = len(d.events)
                result = c.run(parsed, params)
                self.assertEqual(d.events[before], (method, (), params))
                self.assertTrue(result["ack"]["accepted"])
                if name.endswith(".set"):
                    self.assertTrue(result["verified"])
            self.assertEqual(sum(e[0] == "claim" for e in d.events), 1)
        finally:
            c.close()
        self.assertFalse(d.owned)

    def test_busy_keeps_session_and_never_replays_or_stops(self):
        d = FakeDevice()
        c = example.Console(d, heartbeat_interval=0.01)
        try:
            c.run("claim")
            d.busy = True
            with self.assertRaises(BusyError):
                c.run("speed", {"level": 3})
            time.sleep(0.04)
            self.assertEqual(c.run("epc")["epc_count"], 7)
            self.assertTrue(d.owned)
            self.assertEqual(sum(e[0] == "set_speed" for e in d.events), 1)
            self.assertFalse(any(e[0] in ("release", "stop", "reset") for e in d.events))
            self.assertTrue(any(e[0] == "heartbeat" for e in d.events))
        finally:
            c.close()

    def test_pending_reader_renews_and_timeout_does_not_repeat(self):
        d = FakeDevice()
        c = example.Console(d, heartbeat_interval=0.01, reader_timeout=0.12)
        try:
            c.run("claim")
            d.pending_until = time.monotonic() + 1
            with self.assertRaises(UnknownOutcome):
                c.run("antenna.power.get")
            self.assertGreater(sum(e[0] == "heartbeat" for e in d.events), 1)
            self.assertEqual(sum(e[0] == "get_antenna_power" for e in d.events), 1)
        finally:
            c.close()

    def test_readback_mismatch_never_repeats_set(self):
        d = FakeDevice()
        c = example.Console(d)
        try:
            c.run("claim")
            d.mismatch = True
            with self.assertRaisesRegex(ProtocolError, "readback mismatch"):
                c.run(*example.parse_command("antenna.power.set 20 21 22 23 1"))
            self.assertEqual(sum(e[0] == "set_antenna_power" for e in d.events), 1)
        finally:
            c.close()

    def test_owner_loss_never_reclaims(self):
        d = FakeDevice()
        c = example.Console(d)
        c.run("claim")
        d.owned = False
        with self.assertRaisesRegex(ProtocolError, "Ownership lost"):
            c._renew()
        with self.assertRaises(ProtocolError):
            c.run("confirm")
        c.close()
        self.assertEqual([e[0] for e in d.events], ["claim"])

    def test_invalid_commands_rejected_without_device(self):
        for line in (
            "gate 2",
            "speed 0",
            "reset 1",
            "antenna.power.set 31 1 1 1 1",
            "antenna.mask.set 0 1",
            "antenna.return_loss 915001 0",
            "bogus",
        ):
            with self.subTest(line=line), self.assertRaises(ValueError):
                example.parse_command(line)


class BusyTests(unittest.TestCase):
    def test_mqtt_busy_ack_not_retried(self):
        for code, exception in ((5, 0), (6, 6)):
            with self.subTest(code=code):
                m = Mqtt.__new__(Mqtt)
                m.lock = threading.RLock()
                m.condition = threading.Condition()
                m.sequence, m.timeout = 0, 1
                m.client_id, m.root = "1111111111111111", "test/tunnel"
                m.state = dict(session="0123456789abcdef", epoch=1, ms=100)
                m.status = Mock()
                m._fresh = Mock(return_value=True)
                m.client = Mock()
                ack = dict(code=code, exception=exception, result=0)

                def publish(*args, **kwargs):
                    m.pending = (m.pending[0], ack)
                    return Mock(rc=0)

                m.client.publish.side_effect = publish
                with self.assertRaises(BusyError) as caught:
                    m.execute(9, [1])
                self.assertEqual(caught.exception.code, code)
                self.assertEqual(caught.exception.exception, exception)
                m.client.publish.assert_called_once()
                self.assertIsNone(m.pending)

    def test_application_result_remains_compatible_and_can_raise(self):
        ack = dict(accepted=False, result=2, sequence=7)
        with self.assertRaises(BusyError) as caught:
            require_accepted(ack)
        self.assertEqual(caught.exception.result, 2)
        self.assertEqual(ack, dict(accepted=False, result=2, sequence=7))
        with self.assertRaises(ProtocolError) as bad:
            require_accepted(dict(accepted=False, result=1))
        self.assertNotIsInstance(bad.exception, BusyError)

    def test_tcp_and_rtu_busy_frames_do_not_close_or_retry(self):
        for serial in (False, True):
            with self.subTest(serial=serial):
                m = Modbus.__new__(Modbus)
                m.lock = threading.RLock()
                m.serial, m.unit, m.tid = serial, 1, 0
                m.silent_interval = 0
                m.stream = Mock()
                m.stream.write.side_effect = lambda data: len(data)
                if serial:
                    raw = bytes([1, 0x86, 6])
                    chunks = [raw[:2], raw[2:] + struct.pack("<H", crc16(raw))]
                else:
                    chunks = [struct.pack(">HHHB", 1, 0, 3, 1), bytes([0x86, 6])]
                m._exact = Mock(side_effect=chunks)
                with self.assertRaises(BusyError) as caught:
                    m.claim()
                self.assertEqual(caught.exception.exception, 6)
                m.stream.close.assert_not_called()
                self.assertEqual((m.stream.write if serial else m.stream.sendall).call_count, 1)


if __name__ == "__main__":
    unittest.main()
