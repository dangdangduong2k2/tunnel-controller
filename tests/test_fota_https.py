"""Real localhost HTTPS/signing tests; does not claim ESP flash/rollback coverage."""

import importlib.util
from pathlib import Path
import socket
import ssl
import sys
import tempfile
import threading
import unittest
from urllib.request import urlopen
from urllib.error import HTTPError, URLError

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "api"))
from tunnel_sdk.fota import canonical, sign_request
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools/fota" / (name + ".py"))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


class LocalHttpsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        cls.keys = cls.root / "keys"
        module("provision").create("127.0.0.1", cls.keys, cls.root / "Trust.h")
        cls.files = cls.root / "files"
        cls.files.mkdir()
        cls.data = b"\xe9" + bytes(range(256)) * 100
        (cls.files / "test.bin").write_bytes(cls.data)
        (cls.files / "secret.key").write_text("must not be served")
        cls.service = module("https_server").server(
            cls.files, cls.keys / "server.pem", cls.keys / "server.key", port=0
        )
        cls.thread = threading.Thread(target=cls.service.serve_forever, daemon=True)
        cls.thread.start()
        cls.port = cls.service.server_port
        cls.base = f"https://127.0.0.1:{cls.port}"
        cls.context = ssl.create_default_context(cafile=cls.keys / "ca.pem")

    @classmethod
    def tearDownClass(cls):
        cls.service.shutdown()
        cls.service.server_close()
        cls.thread.join()
        cls.temp.cleanup()

    def test_https_exact_file(self):
        with urlopen(self.base + "/firmware/test.bin", context=self.context, timeout=3) as r:
            self.assertEqual(r.status, 200)
            self.assertEqual(int(r.headers["Content-Length"]), len(self.data))
            self.assertEqual(r.read(), self.data)

    def test_untrusted_ca_rejected(self):
        with self.assertRaises(URLError):
            urlopen(self.base + "/firmware/test.bin", timeout=3)

    def test_wrong_hostname_rejected(self):
        with socket.create_connection(("127.0.0.1", self.port), timeout=3) as raw:
            with self.assertRaises(ssl.SSLCertVerificationError):
                self.context.wrap_socket(raw, server_hostname="wrong.invalid")

    def test_missing_and_private_and_traversal_rejected(self):
        for path in (
            "/firmware/missing.bin",
            "/firmware/secret.key",
            "/firmware/%2e%2e/keys/server.key",
            "/",
            "/firmware/../test.bin",
        ):
            with self.subTest(path=path), self.assertRaises(HTTPError) as e:
                urlopen(self.base + path, context=self.context, timeout=3)
            self.assertEqual(e.exception.code, 404)

    def test_signature_binds_all_manifest_fields(self):
        q = dict(
            job="a" * 32,
            session="b" * 16,
            client="c" * 16,
            epoch=3,
            id=1,
            seen_ms=100,
            url="https://127.0.0.1:8443/firmware/v.bin",
            target="tunnel-esp32s3-v1",
            version=1131,
            size=1000000,
            sha256="d" * 64,
            unix_time=1794340000,
        )
        sig = bytes.fromhex(sign_request(q, self.keys / "release.key"))
        key = serialization.load_pem_public_key((self.keys / "release-public.pem").read_bytes())
        key.verify(sig, canonical(q), ec.ECDSA(hashes.SHA256()))
        for field in q:
            changed = dict(q)
            changed[field] = (
                changed[field] + 1 if isinstance(changed[field], int) else changed[field] + "x"
            )
            with self.subTest(field=field), self.assertRaises(InvalidSignature):
                key.verify(sig, canonical(changed), ec.ECDSA(hashes.SHA256()))

    def test_private_key_not_in_firmware_header(self):
        header = (self.root / "Trust.h").read_text()
        self.assertNotIn("PRIVATE KEY", header)
        self.assertIn("PUBLIC KEY", header)
        with self.assertRaises(ValueError):
            module("provision").create("127.0.0.1", self.keys, self.root / "Trust.h")


if __name__ == "__main__":
    unittest.main()
