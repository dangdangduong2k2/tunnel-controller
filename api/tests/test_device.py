"""Public SDK contract; fake transports only, no hardware or broker access."""

import unittest
from unittest.mock import patch
from tunnel_sdk import TunnelDevice, ProtocolError, ReaderError, UnknownOutcome
from tunnel_sdk.status import decode_status
from tunnel_sdk.operations import CATALOG


class Transport:
    def __init__(self, **kwargs):
        self.stream = object()
        self.connected = self.subscribed = True
        self.events = []
        self.kwargs = kwargs
        self.regs = [0] * 96
        self.regs[0:2] = [3, 1110]
        self.regs[10] = 7
        self.regs[20:24] = [2, 123, 0, 6]
        self.regs[32:38] = [148, 0, 20, 21, 22, 23]

    def status(self):
        self.events.append(("status",))
        return decode_status(self.regs)

    def execute(self, op, args):
        self.events.append(("execute", op, args))
        return dict(accepted=True, result=0, sequence=123)

    def close(self):
        self.events.append(("close",))
        self.stream = None


# Independently specified wire contract, not generated from facade code.
CASES = [
    ("reset", (), {}, 1, []),
    ("set_gate", (1,), {}, 2, [1]),
    ("set_belt", (0,), {}, 3, [0]),
    ("set_scan", (1,), {}, 4, [1]),
    ("set_mode", (1,), {}, 5, [1]),
    ("confirm", (), {}, 6, []),
    ("rescan", (), {}, 7, []),
    ("set_speed", (3,), {}, 8, [3]),
    ("set_maintenance", (1,), {}, 9, [1]),
    ("get_reader_info", (), {}, 20, []),
    ("get_antenna_power", (), {}, 21, []),
    ("set_antenna_power", (20, 21, 22, 23), {"ram_only": 1}, 22, [20, 21, 22, 23, 1]),
    ("set_antenna_mask", (5,), {"ram_only": 0}, 23, [5, 0]),
    ("get_rf_profile", (), {}, 24, []),
    ("set_rf_profile", (128,), {}, 25, [128]),
    ("get_gen2x", (), {}, 26, []),
    ("set_gen2x", (4123,), {}, 27, [4123]),
    ("get_ex10", (), {}, 28, []),
    ("measure_return_loss", (915000, 3), {}, 29, [13, 63032, 3]),
]


class DeviceTests(unittest.TestCase):
    def test_all_operations_all_transports_numeric(self):
        self.assertEqual({c[3] for c in CASES}, {o["op"] for o in CATALOG.values()})
        for transport in ("tcp", "rtu", "mqtt"):
            with (
                patch("tunnel_sdk.device.Modbus", Transport),
                patch("tunnel_sdk.device.Mqtt", Transport),
            ):
                with TunnelDevice(transport, host="simulated") as device:
                    for name, args, kwargs, op, values in CASES:
                        with self.subTest(transport=transport, name=name):
                            ack = getattr(device, name)(*args, **kwargs)
                            self.assertTrue(ack["accepted"])
                            self.assertEqual(device._client.events[-1], ("execute", op, values))

    def test_lazy_connect_context_and_close_do_not_send_control(self):
        with patch("tunnel_sdk.device.Modbus", Transport) as factory:
            d = TunnelDevice.tcp("simulated")
            self.assertFalse(d.is_connected())
            with self.assertRaises(ProtocolError):
                d.get_status()
            with d:
                client = d._client
                self.assertIs(d.connect(), d)
                self.assertEqual(d.get_epc_count(), 7)
                self.assertTrue(d.is_connected())
            d.close()
            self.assertEqual(client.events, [("status",), ("close",)])

    def test_invalid_named_parameters_rejected_before_io(self):
        d = TunnelDevice.tcp("simulated")
        for method, params in [
            ("speed", {"level": 0}),
            ("gate", {"close": True}),
            ("antenna.power.set", {}),
            ("unknown", {}),
        ]:
            with self.assertRaises(ValueError):
                d.send_command(method, **params)
        for frequency in (839999, 960001, 915001, True):
            with self.assertRaises(ValueError):
                d.measure_return_loss(frequency, 0)
        for t in (0, -1, True, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                TunnelDevice.tcp("simulated", timeout=t)

    def test_reader_pending_mismatch_and_valid_payload(self):
        with patch("tunnel_sdk.device.Modbus", Transport):
            with TunnelDevice.tcp("simulated") as d:
                self.assertIsNone(d.get_reader_result(122))
                d._client.regs[20] = 1
                self.assertIsNone(d.get_reader_result(123))
                d._client.regs[20] = 2
                result = d.get_reader_result(123, expected_command=148)
                self.assertEqual(result["data"], [20, 21, 22, 23])
                self.assertTrue(all(e == ("status",) for e in d._client.events))

    def test_reader_errors_not_reported_as_success(self):
        for index, value in [(20, 3), (22, 6), (23, 1), (33, 1), (34, 256)]:
            with patch("tunnel_sdk.device.Modbus", Transport):
                with TunnelDevice.tcp("simulated") as d:
                    d._client.regs[index] = value
                    with self.assertRaises(ReaderError):
                        d.get_reader_result(123)
        with patch("tunnel_sdk.device.Modbus", Transport):
            with TunnelDevice.tcp("simulated") as d:
                with self.assertRaises(ReaderError):
                    d.get_reader_result(123, expected_command=33)

    def test_unknown_outcome_not_retried(self):
        class Lost(Transport):
            def execute(self, op, args):
                super().execute(op, args)
                raise UnknownOutcome("ACK lost")

        with patch("tunnel_sdk.device.Modbus", Lost):
            with TunnelDevice.tcp("simulated") as d:
                with self.assertRaises(UnknownOutcome):
                    d.set_belt(1)
                self.assertEqual(d._client.events, [("execute", 3, [1])])

    def test_function_catalog_is_detached(self):
        catalog = TunnelDevice.get_functions()
        catalog["speed"]["params"][0][1] = 0
        self.assertEqual(TunnelDevice.get_functions()["speed"]["params"][0][1], 1)


if __name__ == "__main__":
    unittest.main()
