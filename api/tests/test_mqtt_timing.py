"""Bounded token reuse reduces GETs without retries or invented timestamps."""

import json
import threading
import unittest
from unittest.mock import Mock, patch
from tunnel_sdk import Mqtt, ProtocolError


class TimingTests(unittest.TestCase):
    def client(self):
        m = Mqtt.__new__(Mqtt)
        m.lock = threading.RLock()
        m.condition = threading.Condition()
        m.connected = True
        m.sequence, m.timeout = 0, 5
        m.client_id, m.root = "1" * 16, "test"
        m.received, m.read_started = 100, 99
        registers = [0] * 96
        registers[0], registers[1], registers[16] = 3, 1123, 6
        m.state = dict(
            session="a" * 16, epoch=1, ms=1234, owner_client=m.client_id, registers=registers
        )
        m.status = Mock()
        m.client = Mock()

        def publish(*args, **kwargs):
            m.pending = (m.pending[0], dict(code=0, exception=0, result=0))
            return Mock(rc=0)

        m.client.publish.side_effect = publish
        return m

    @patch("tunnel_sdk.mqtt.time.monotonic", return_value=100.5)
    def test_recent_state_reused_for_owner_and_heartbeat(self, clock):
        m = self.client()
        self.assertTrue(m.has_control())
        self.assertTrue(m.heartbeat()["accepted"])
        m.status.assert_not_called()
        self.assertEqual(m.client.publish.call_count, 1)
        self.assertEqual(json.loads(m.client.publish.call_args.args[1])["seen_ms"], 1234)

    @patch("tunnel_sdk.mqtt.time.monotonic", return_value=100.5)
    def test_cache_age_flight_time_and_old_firmware(self, clock):
        for field, value in (("received", 99), ("read_started", 97)):
            m = self.client()
            setattr(m, field, value)
            self.assertFalse(m._cached_state())
        m = self.client()
        m.state["registers"][1] = 1122
        self.assertFalse(m._cached_state())
        m.heartbeat()
        m.status.assert_called_once()

    @patch("tunnel_sdk.mqtt.time.monotonic", return_value=100.5)
    def test_rejected_token_invalidates_cache_without_retry(self, clock):
        m = self.client()

        def reject(*args, **kwargs):
            m.pending = (m.pending[0], dict(code=3, exception=0, result=0))
            return Mock(rc=0)

        m.client.publish.side_effect = reject
        with self.assertRaisesRegex(ProtocolError, "MQTT code 3"):
            m.heartbeat()
        self.assertIsNone(m.state)
        self.assertEqual(m.client.publish.call_count, 1)

    @patch("tunnel_sdk.mqtt.time.monotonic", return_value=100.5)
    def test_release_invalidates_cached_owner(self, clock):
        m = self.client()
        m.release()
        self.assertIsNone(m.state)


if __name__ == "__main__":
    unittest.main()
