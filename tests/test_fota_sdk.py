"""FOTA SDK guards and dispatch semantics; no real broker or device."""

from pathlib import Path
import json, sys, tempfile, threading, unittest
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "api"))
from tunnel_sdk.fota import FotaClient, signing_key_id, canonical
from tunnel_sdk.errors import ProtocolError, UnknownOutcome
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.asymmetric import ec
from test_fota_portal import image


class SDKTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.key = Path(self.temp.name) / "release.key"
        self.private = ec.generate_private_key(ec.SECP256R1())
        self.key.write_bytes(
            self.private.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            )
        )
        self.app = Path(self.temp.name) / "app.bin"
        self.app.write_bytes(image(version=1161))
        self.c = object.__new__(FotaClient)
        c = self.c
        c.lock = threading.RLock()
        c.update_lock = threading.Lock()
        c.condition = threading.Condition()
        c.sequence = 0
        c.client_id = "a" * 16
        c.root = "test/board/v1"
        c.timeout = 0.02
        c.connected = True
        c.update_expected = None
        c.update_ack = None
        c.state = dict(session="b" * 16, epoch=1, ms=123)
        c.status = lambda: dict(owner=0, activity_flags=0, firmware=1160)
        self.cap = dict(
            configured=True,
            storage_ready=True,
            ota_revision=2,
            url_prefix="https://example.com/firmware/",
            signing_key_id=signing_key_id(self.key),
        )
        c.get_update_status = lambda: self.cap
        self.sent = []
        self.ack = True

        def publish(topic, payload, **kw):
            self.sent.append((topic, json.loads(payload), kw))
            if self.ack:
                c.update_ack = dict(
                    job=c.update_expected["job"],
                    client=c.client_id,
                    id=c.sequence,
                    state="accepted",
                )
            return SimpleNamespace(rc=0)

        c.client = SimpleNamespace(publish=publish)

    def update(self, **extra):
        args = dict(
            url="https://example.com/firmware/app.bin",
            firmware=self.app,
            version=1161,
            signing_key=self.key,
        )
        args.update(extra)
        return self.c.update_firmware(**args)

    def test_one_signed_dispatch(self):
        self.assertEqual(self.update()["state"], "accepted")
        self.assertEqual(len(self.sent), 1)
        topic, q, kw = self.sent[0]
        self.assertTrue(topic.endswith("/ota/cmd"))
        self.assertEqual(kw, dict(qos=1, retain=False))
        self.private.public_key().verify(
            bytes.fromhex(q["signature"]), canonical(q), ec.ECDSA(hashes.SHA256())
        )

    def test_version_is_from_image(self):
        with self.assertRaises(ValueError):
            self.update(version=9999)
        self.assertEqual(self.sent, [])

    def test_reinstall_and_downgrade_are_signed_once(self):
        for running in (1161, 1162):
            with self.subTest(running=running):
                self.c.status = lambda: dict(owner=0, activity_flags=0, firmware=running)
                self.sent.clear()
                self.assertEqual(self.update()["state"], "accepted")
                self.assertEqual(len(self.sent), 1)
                self.assertEqual(self.sent[0][1]["version"], 1161)
                self.assertEqual(self.sent[0][2], dict(qos=1, retain=False))

    def test_capability_mismatch_never_publishes(self):
        for field, value in [
            ("ota_revision", 1),
            ("configured", False),
            ("storage_ready", False),
            ("signing_key_id", "wrong"),
            ("url_prefix", "https://different.example/"),
        ]:
            old = self.cap[field]
            self.cap[field] = value
            with self.assertRaises(ProtocolError):
                self.update()
            self.cap[field] = old
        self.assertEqual(self.sent, [])

    def test_busy_never_publishes(self):
        self.c.status = lambda: dict(owner=1, activity_flags=0, firmware=1160)
        with self.assertRaises(ProtocolError):
            self.update()
        self.assertEqual(self.sent, [])

    def test_lost_ack_is_unknown_and_not_retried(self):
        self.ack = False
        with self.assertRaises(UnknownOutcome):
            self.update()
        self.assertEqual(len(self.sent), 1)

    def test_reconnect_clears_ota_cache(self):
        self.c.update_state = {"state": "success"}
        self.c.update_received = 999
        mqtt = SimpleNamespace(subscribe=lambda *args: None)
        self.c._connect(mqtt, None, None, 0, None)
        self.assertIsNone(self.c.update_state)
        self.assertEqual(self.c.update_received, 0)


if __name__ == "__main__":
    unittest.main()
