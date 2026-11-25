"""Portal HTTP + job workflow tests. Fake board; no live MQTT command is sent."""

import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "fota"))
from portal import create_app, validate_image
from tunnel_sdk.errors import UnknownOutcome
from tunnel_sdk.firmware_image import MAGIC, IDENTITY
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def image(version=1161, target=b"tunnel-esp32s3-v1", duplicate=False, metadata=True):
    raw = bytearray(288)
    raw[0], raw[1], raw[12], raw[23] = 0xE9, 1, 9, 1
    struct.pack_into("<I", raw, 28, 256)
    raw[32:36] = bytes.fromhex("3254cdab")
    if metadata:
        record = IDENTITY.pack(MAGIC, 1, version, target.ljust(32, b"\0"))
        raw[64 : 64 + len(record)] = record
        if duplicate:
            raw[128 : 128 + len(record)] = record
    checksum = 0xEF
    for b in raw[32:]:
        checksum ^= b
    raw.extend(bytes(15) + bytes([checksum]))
    return bytes(raw) + hashlib.sha256(raw).digest()


class Board:
    outcome = None
    updates = 0
    status_reply = {"job": "", "state": "idle"}

    def __init__(self, *args, **kwargs):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def status(self):
        return {"firmware": 1160}

    def update_firmware(self, **kwargs):
        Board.updates += 1
        if Board.outcome:
            raise Board.outcome
        Board.status_reply = {
            "job": kwargs["job"],
            "state": "success",
            "running_version": kwargs["version"],
            "percent": 0,
        }
        return {"state": "accepted"}

    def get_update_status(self):
        return Board.status_reply


class PortalTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.key = Path(self.temp.name) / "private.key"
        self.key.write_bytes(
            ec.generate_private_key(ec.SECP256R1()).private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            )
        )
        self.config = {
            "TESTING": True,
            "SIGNING_KEY": str(self.key),
            "PUBLIC_BASE": "https://example.com/tunnel/firmware/",
            "DISCOVERY_START": False,
            "POLL_SECONDS": 0,
            "MONITOR_SECONDS": 0.1,
        }
        Board.outcome, Board.updates = None, 0
        Board.status_reply = {"job": "", "state": "idle"}
        self.app = create_app(self.temp.name, Board, self.config)
        self.c = self.app.test_client()
        self.headers = {"X-Tunnel-Request": "1"}

    def announce(self, device="aabbccddeeff", root=None, retained=False, firmware=1160):
        discovery = self.app.extensions["discovery"]
        discovery.connected = True
        return discovery.receive(
            "tunnel/discovery/v1/" + device,
            json.dumps(
                {
                    "discovery_version": 1,
                    "device_id": device,
                    "model": "tunnel-esp32s3-v1",
                    "root": root or "test/" + device + "/v1",
                    "firmware": firmware,
                }
            ).encode(),
            retained,
        )

    def test_no_seeded_devices(self):
        self.assertEqual(self.overview()["devices"], [])
        self.assertEqual(self.post("status", json={"device_id": "aabbccddeeff"}).status_code, 400)

    def test_retained_heartbeat_expiry_and_offline(self):
        disc = self.app.extensions["discovery"]
        disc.clock = lambda: 1000
        self.announce(retained=True)
        self.assertFalse(self.overview()["devices"][0]["online"])
        self.announce()
        self.assertTrue(self.overview()["devices"][0]["online"])
        disc.clock = lambda: 1046
        self.assertFalse(self.overview()["devices"][0]["online"])
        self.announce()
        self.assertTrue(self.overview()["devices"][0]["online"])
        disc.receive("test/aabbccddeeff/v1/availability", b'{"online":false}', True)
        self.assertFalse(self.overview()["devices"][0]["online"])
        disc.receive("test/aabbccddeeff/v1/availability", b'{"online":true}', True)
        self.assertFalse(self.overview()["devices"][0]["online"])

    def test_multi_board_target_and_duplicate_roots(self):
        body = self.prepare_job()
        self.announce("112233445566")
        with patch("portal.threading.Thread") as thread:
            result = self.post(
                "deploy", json={**body, "device_id": "112233445566", "request_id": "b" * 32}
            )
            self.assertEqual(result.status_code, 202)
            self.assertEqual(thread.call_args.kwargs["args"][2], "test/112233445566/v1")
        self.announce("778899aabbcc", "test/aabbccddeeff/v1")
        self.assertEqual(self.post("status", json={"device_id": "aabbccddeeff"}).status_code, 400)
        self.assertTrue(
            next(d for d in self.overview()["devices"] if d["device_id"] == "aabbccddeeff")[
                "conflict"
            ]
        )

    def test_invalid_discovery_never_adds_device(self):
        disc = self.app.extensions["discovery"]
        for payload in (b"not json", b"[]", b"{}", b"x" * 513):
            self.assertFalse(disc.receive("tunnel/discovery/v1/aabbccddeeff", payload))
        self.assertFalse(self.announce(root="test/+/cmd"))
        self.assertFalse(self.announce(device="wrong-id"))
        self.assertEqual(self.overview()["devices"], [])

    def post(self, action, **kwargs):
        return self.c.post("/tunnel/admin/api/" + action, headers=self.headers, **kwargs)

    def upload(self, raw=None, version="1161", name="app.bin"):
        return self.post(
            "upload", data={"version": version, "file": (io.BytesIO(raw or image()), name)}
        )

    def overview(self):
        return self.c.get("/tunnel/admin/api/overview").json

    def prepare_job(self):
        self.announce()
        sha = self.upload().json["sha"]
        body = {"sha": sha, "request_id": "a" * 32, "device_id": "aabbccddeeff"}
        with patch("portal.threading.Thread"):
            r = self.post("deploy", json=body)
        self.assertEqual(r.status_code, 202)
        return body

    def test_page_no_login_and_no_secrets(self):
        r = self.c.get("/tunnel/admin/")
        self.assertEqual(r.status_code, 200)
        self.assertNotIn(b'type="password"', r.data)
        r.close()
        self.assertNotIn(b"secret-not-for-browser", self.c.get("/tunnel/admin/api/overview").data)
        self.assertEqual(self.c.get("/tunnel/firmware/private.key").status_code, 404)

    def test_browser_origin_and_header(self):
        self.assertEqual(self.c.post("/tunnel/admin/api/status").status_code, 403)
        self.assertEqual(
            self.c.post(
                "/tunnel/admin/api/status",
                headers={**self.headers, "Origin": "https://evil.example"},
            ).status_code,
            403,
        )
        self.announce()
        self.assertEqual(self.post("status", json={"device_id": "aabbccddeeff"}).status_code, 200)

    def test_upload_download_and_idempotency(self):
        r = self.upload(name="../../app.bin")
        self.assertEqual(r.status_code, 201)
        data = self.c.get("/tunnel/firmware/" + r.json["sha"] + ".bin")
        self.assertEqual(data.data, image())
        self.assertEqual(data.content_length, len(image()))
        data.close()
        self.assertEqual(self.upload().status_code, 200)
        self.assertEqual(self.upload(version="1131").status_code, 400)
        self.assertEqual(len(self.overview()["releases"]), 1)
        self.assertEqual(self.overview()["releases"][0]["name"], "app.bin")

    def test_reject_wrong_chip_truncation_corruption(self):
        for raw in (image()[:100], b"not firmware", image()[:-1], image()[:-1] + b"x"):
            self.assertEqual(self.upload(raw=raw).status_code, 400)
        raw = bytearray(image())
        raw[12] = 0
        with self.assertRaises(ValueError):
            validate_image(raw)
        self.assertEqual(self.upload(version="0").status_code, 400)
        self.assertEqual(self.upload(name="secret.pem").status_code, 400)

    def test_missing_setup_blocks_deploy(self):
        self.app.config["PUBLIC_BASE"] = "http://example.com/"
        self.assertEqual(self.post("deploy", json={}).status_code, 409)
        self.assertFalse(self.overview()["ready"])

    def test_auto_version_and_embedded_identity(self):
        r = self.post("upload", data={"file": (io.BytesIO(image()), "release.bin")})
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json["version"], 1161)
        for raw in (image(metadata=False), image(target=b"other-board"), image(duplicate=True)):
            self.assertEqual(self.upload(raw=raw).status_code, 400)
        self.assertEqual(self.upload(version="9999").status_code, 400)

    def test_invalid_key_is_not_ready(self):
        self.key.write_text("not a private key")
        overview = self.overview()
        self.assertFalse(overview["ready"])
        self.assertTrue(overview["setup"]["problems"])
        self.assertNotIn("not a private key", str(overview))

    def test_backend_accepts_reinstall_and_downgrade(self):
        sha = self.upload().json["sha"]
        for running, device in [(1161, "aabbccddee01"), (1162, "aabbccddee02")]:
            with self.subTest(running=running):
                self.announce(device=device, firmware=running)
                with patch("portal.threading.Thread") as thread:
                    r = self.post(
                        "deploy",
                        json=dict(sha=sha, device_id=device, request_id=device.ljust(32, "0")),
                    )
                    self.assertEqual(r.status_code, 202)
                    thread.return_value.start.assert_called_once()
                job = next(j for j in self.overview()["jobs"] if j["device_id"] == device)
                self.assertEqual(job["version"], 1161)

    def test_boot_verifying_and_bad_progress(self):
        body = self.prepare_job()
        Board.status_reply = dict(
            job=body["request_id"], state="boot_verifying", percent=100, running_version=1161
        )
        self.post("status", json={"device_id": body["device_id"]})
        self.assertEqual(self.overview()["jobs"][0]["state"], "boot_verifying")
        self.assertEqual(self.overview()["jobs"][0]["active"], 1)
        Board.status_reply["percent"] = "bad"
        self.assertEqual(
            self.post("status", json={"device_id": body["device_id"]}).status_code, 200
        )

    def test_double_click_and_busy_do_not_send_twice(self):
        body = self.prepare_job()
        with patch("portal.threading.Thread") as thread:
            self.assertEqual(self.post("deploy", json=body).status_code, 200)
            self.assertEqual(
                self.post("deploy", json={**body, "request_id": "b" * 32}).status_code, 409
            )
            thread.assert_not_called()

    def test_success_from_matching_board_job(self):
        body = self.prepare_job()
        self.app.extensions["fota_run_job"](
            body["request_id"], self.overview()["releases"][0], "test/aabbccddeeff/v1"
        )
        j = self.overview()["jobs"][0]
        self.assertEqual((j["state"], j["active"], j["percent"]), ("success", 0, 100))
        self.assertEqual(Board.updates, 1)

    def test_unknown_outcome_locked_until_matching_status(self):
        body = self.prepare_job()
        Board.outcome = UnknownOutcome("lost ACK")
        self.app.extensions["fota_run_job"](
            body["request_id"], self.overview()["releases"][0], "test/aabbccddeeff/v1"
        )
        self.assertEqual(self.overview()["jobs"][0]["state"], "unknown")
        Board.status_reply = {"job": "b" * 32, "state": "success", "running_version": 1130}
        self.post("status", json={"device_id": "aabbccddeeff"})
        self.assertEqual(self.overview()["jobs"][0]["active"], 1)
        Board.status_reply = {
            "job": body["request_id"],
            "state": "success",
            "running_version": 1123,
        }
        self.post("status", json={"device_id": "aabbccddeeff"})
        self.assertEqual(self.overview()["jobs"][0]["active"], 1)
        Board.status_reply["running_version"] = 1161
        self.post("status", json={"device_id": "aabbccddeeff"})
        self.assertEqual(self.overview()["jobs"][0]["active"], 0)
        self.assertEqual(Board.updates, 1)

    def test_restart_does_not_replay(self):
        self.prepare_job()
        app = create_app(self.temp.name, Board, self.config)
        j = app.test_client().get("/tunnel/admin/api/overview").json["jobs"][0]
        self.assertEqual((j["state"], j["active"]), ("unknown", 1))
        self.assertEqual(Board.updates, 0)

    def test_legacy_board_requires_usb_bootstrap(self):
        self.announce(firmware=1151)
        sha = self.upload().json["sha"]
        with patch("portal.threading.Thread") as thread:
            response = self.post(
                "deploy", json=dict(sha=sha, device_id="aabbccddeeff", request_id="c" * 32)
            )
            self.assertEqual(response.status_code, 400)
            self.assertIn("USB", response.json["error"])
            thread.assert_not_called()


if __name__ == "__main__":
    unittest.main()
