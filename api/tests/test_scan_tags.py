"""Standalone RF workflow never sends Reset, homing, motion or maintenance."""

import sys
from pathlib import Path
import unittest

from tunnel_sdk import ProtocolError
from test_examples import FakeDevice

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "example"))
from scan_tags import scan_tags


class ReaderDevice(FakeDevice):
    def __init__(self):
        super().__init__()
        self.firmware = 1122
        self.scan = False
        self.error = 0
        self.count = 0

    def get_status(self):
        if self.scan:
            self.count += 1
        return dict(
            firmware=self.firmware,
            owner=int(self.owned),
            activity_flags=8 if self.scan else 0,
            reader_state=0,
            reader_flags=2 if self.scan else 1,
            scan_result=self.error or int(self.scan),
            scan_elapsed_ms=100,
            epc_count=self.count,
            total_reads=self.count * 2,
        )

    def set_scan(self, run):
        self.events.append(("set_scan", (), {"run": run}))
        self.scan = bool(run)
        return dict(accepted=True, result=0)


class ScanTests(unittest.TestCase):
    def test_rf_only_workflow(self):
        device = ReaderDevice()
        output = []
        result = scan_tags(device, seconds=0.01, emit=output.append)
        self.assertGreater(result["epc_count"], 0)
        self.assertFalse(device.owned)
        self.assertEqual(
            [e[0] for e in device.events], ["claim", "set_scan", "set_scan", "release"]
        )
        self.assertEqual([e[2]["run"] for e in device.events if e[0] == "set_scan"], [1, 0])
        self.assertTrue(output)

    def test_old_firmware_and_foreign_owner_no_claim(self):
        device = ReaderDevice()
        device.firmware = 1121
        with self.assertRaises(ProtocolError):
            scan_tags(device)
        device.firmware = 1122
        device.owned = True
        with self.assertRaises(ProtocolError):
            scan_tags(device)
        self.assertEqual(device.events, [])

    def test_reader_error_releases_without_reset_or_retry(self):
        device = ReaderDevice()
        device.error = 5
        with self.assertRaisesRegex(ProtocolError, "Reader scan error 5"):
            scan_tags(device, seconds=0.01)
        self.assertEqual([e[0] for e in device.events], ["claim", "set_scan", "release"])
        self.assertFalse(device.owned)

    def test_cleanup_failure_does_not_hide_scan_failure(self):
        class BrokenCleanup(ReaderDevice):
            def has_control(self):
                if self.scan:
                    raise ProtocolError("Connection closed during cleanup")
                return super().has_control()

        device = BrokenCleanup()
        device.error = 5
        with self.assertRaisesRegex(ProtocolError, "Reader scan error 5"):
            scan_tags(device, seconds=0.01)
        self.assertEqual([e[0] for e in device.events], ["claim", "set_scan"])


if __name__ == "__main__":
    unittest.main()
