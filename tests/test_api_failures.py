"""Failure semantics: never replay uncertain motion; validate names before network IO."""

import json
import pathlib
import sys
import threading
import types
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "api"))
from tunnel_api import Mqtt, Modbus, ProtocolError, UnknownOutcome, decode_status


class Failures(unittest.TestCase):
    def test_modbus_corrupt_response_closes_without_retry(self):
        class Stream:
            sends = 0
            closed = False

            def sendall(self, data):
                self.sends += 1

            def recv(self, n):
                return b"\0" * n

            def close(self):
                self.closed = True

        stream = Stream()
        m = Modbus.__new__(Modbus)
        m.unit = 1
        m.tid = 0
        m.lock = threading.RLock()
        m.serial = False
        m.stream = stream
        with self.assertRaises(UnknownOutcome):
            m.stop()
        with self.assertRaises(UnknownOutcome):
            m.stop()
        self.assertEqual(stream.sends, 1)
        self.assertTrue(stream.closed)

    def test_mqtt_ack_loss_sends_once_without_heartbeat(self):
        class Client:
            requests = []

            def publish(self, topic, payload, **kwargs):
                self.requests.append(json.loads(payload))
                return types.SimpleNamespace(rc=0)

        m = Mqtt.__new__(Mqtt)
        m.lock = threading.RLock()
        m.condition = threading.Condition()
        m.sequence = 0
        m.root = "test"
        m.timeout = 0.01
        m.client_id = "1" * 16
        m.client = Client()
        m.connected = True
        m.status = lambda: None
        m._fresh = lambda: True
        m.state = dict(session="a" * 16, epoch=1, ms=100)
        with self.assertRaises(UnknownOutcome):
            m.call("speed", level=3)
        self.assertEqual(len(m.client.requests), 1)
        self.assertEqual(m.client.requests[0]["method"], "speed")
        self.assertIsNone(m.pending)

    def test_named_params_rejected_before_io(self):
        for cls in (Modbus, Mqtt):
            m = cls.__new__(cls)
            for method, params in [
                ("speed", {"level": 0}),
                ("speed", {"level": True}),
                ("speed", {}),
                ("speed", {"level": 3, "extra": 0}),
                ("antenna.power.get", {"dummy": 0}),
                ("not_a_method", {}),
            ]:
                with self.assertRaises(ValueError):
                    m.call(method, **params)

    def test_mqtt_only_correlated_nonretained_state(self):
        m = Mqtt.__new__(Mqtt)
        m.root = "test"
        m.condition = threading.Condition()
        m.pending = None
        m.state = None
        m.received = 0
        m.connected = True
        m.client_id = "1" * 16
        m.read_pending = (4, None)
        regs = [0] * 96
        regs[0] = 3
        d = dict(
            api_version=1,
            session="a" * 16,
            epoch=1,
            ms=100,
            owner_client="",
            registers=regs,
            request_client=m.client_id,
            request_id=4,
        )

        def send(retain=False):
            m._message(
                None,
                None,
                types.SimpleNamespace(
                    topic="test/state", payload=json.dumps(d).encode(), retain=retain
                ),
            )

        send(True)
        self.assertIsNone(m.state)
        d["request_id"] = 3
        send()
        self.assertIsNone(m.state)
        d["request_id"] = 4
        send()
        self.assertIsNotNone(m.state)
        d["ms"] = 99
        send()
        self.assertEqual(m.state["ms"], 100)

    def test_status_validation(self):
        for regs in ([0] * 96, [3] * 95, [3] * 95 + [True], [3] * 95 + [65536]):
            with self.assertRaises(ProtocolError):
                decode_status(regs)


if __name__ == "__main__":
    unittest.main()
