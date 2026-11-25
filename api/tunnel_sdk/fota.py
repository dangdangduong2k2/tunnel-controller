"""Signed FOTA client. No retry, auto-claim, or implicit motion commands."""

import hashlib
import json
from pathlib import Path
import threading
import time
import uuid
from .mqtt import Mqtt
from .errors import ProtocolError, UnknownOutcome
from .firmware_image import inspect_firmware


def signing_key_id(key_path):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec

    key = serialization.load_pem_private_key(Path(key_path).read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("Use a P-256 release signing key")
    public = key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    return hashlib.sha256(public).hexdigest()


def canonical(request):
    keys = (
        "job",
        "session",
        "epoch",
        "client",
        "id",
        "seen_ms",
        "url",
        "target",
        "version",
        "size",
        "sha256",
        "unix_time",
    )
    return ("TUNNEL-FOTA-1\n" + "\n".join(str(request[k]) for k in keys) + "\n").encode("ascii")


def sign_request(request, key_path):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec

    key = serialization.load_pem_private_key(Path(key_path).read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("Use a P-256 release signing key")
    return key.sign(canonical(request), ec.ECDSA(hashes.SHA256())).hex()


class FotaClient(Mqtt):
    def __init__(self, *args, **kwargs):
        self.update_state = None
        self.update_received = 0
        self.update_ack = None
        self.update_expected = None
        self.update_lock = threading.Lock()
        super().__init__(*args, **kwargs)

    def _connect(self, client, userdata, flags, reason, properties):
        with self.condition:
            self.connected = reason == 0
            self.state = None
            self.subscribed = False
            self.update_state = None
            self.update_received = 0
        if reason == 0:
            client.subscribe(
                [(self.root + s, 1) for s in ("/state", "/ack", "/ota/state", "/ota/ack")]
            )

    def _subscribe(self, client, userdata, mid, reasons, properties):
        with self.condition:
            self.subscribed = len(reasons) == 4 and all(not r.is_failure for r in reasons)
            self.condition.notify_all()

    def _message(self, client, userdata, message):
        if message.topic not in (self.root + "/ota/state", self.root + "/ota/ack"):
            return super()._message(client, userdata, message)
        if message.retain or len(message.payload) > 1536:
            return
        try:
            data = json.loads(message.payload)
            if not isinstance(data, dict) or data.get("api_version") != 1:
                return
            with self.condition:
                if message.topic.endswith("/ota/state"):
                    self.update_state = data
                    self.update_received = time.monotonic()
                elif self.update_expected and all(
                    data.get(k) == self.update_expected[k] for k in ("job", "client", "id")
                ):
                    self.update_ack = data
                self.condition.notify_all()
        except (ValueError, TypeError):
            pass

    def get_update_status(self):
        with self.update_lock:
            started = time.monotonic()
            if self.client.publish(self.root + "/ota/get", "{}", qos=1, retain=False).rc:
                raise ProtocolError("FOTA status publish failed")
            with self.condition:
                while self.update_received < started:
                    left = self.timeout - (time.monotonic() - started)
                    if left <= 0 or not self.connected:
                        raise ProtocolError("No FOTA state; requires FOTA-enabled firmware")
                    self.condition.wait(left)
                return dict(self.update_state)

    def update_firmware(self, *, url, firmware, version, signing_key, job=None):
        if (
            not isinstance(version, int)
            or isinstance(version, bool)
            or not 0 < version <= 0xFFFFFFFF
        ):
            raise ValueError("version must be an integer firmware register")
        if (
            not url.startswith("https://")
            or len(url) > 256
            or any(c.isspace() or c in '\\"@#' or ord(c) > 126 for c in url)
        ):
            raise ValueError("Use a bounded HTTPS URL without credentials/fragments")
        data = Path(firmware).read_bytes()
        identity = inspect_firmware(data)
        if identity["version"] != version:
            raise ValueError("Requested version differs from the firmware image")
        key_path = Path(signing_key)
        # Validate key before network traffic; final signature covers fresh device state.
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec

        key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
        if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
            key.curve, ec.SECP256R1
        ):
            raise ValueError("Use a P-256 release signing key")
        job = job or uuid.uuid4().hex
        if len(job) != 32 or any(c not in "0123456789abcdef" for c in job):
            raise ValueError("job must be 32 lowercase hex characters")
        # Read OTA capability before acquiring update_lock (get_update_status owns it).
        capability = self.get_update_status()
        if capability.get("ota_revision", 1) < 2:
            raise ProtocolError("USB bootstrap required: install firmware 1.16.0 or newer first")
        if not capability.get("configured") or not capability.get("storage_ready"):
            raise ProtocolError("FOTA trust/storage is not ready on the board")
        if capability.get("ota_revision", 1) >= 2:
            if capability.get("signing_key_id") != signing_key_id(key_path):
                raise ProtocolError("Signing key differs from the key trusted by the board")
            prefix = capability.get("url_prefix", "")
            if not prefix or not url.startswith(prefix):
                raise ProtocolError("HTTPS URL differs from the prefix trusted by the board")
        with self.lock, self.update_lock:
            status = self.status()
            if status["owner"] or status["activity_flags"] & 31:
                raise ProtocolError("BUSY: stop activity and release control first")
            self.sequence += 1
            if self.sequence > 0xFFFFFFFF:
                raise ProtocolError("Client ID exhausted")
            request = dict(
                job=job,
                session=self.state["session"],
                epoch=self.state["epoch"],
                client=self.client_id,
                id=self.sequence,
                seen_ms=self.state["ms"],
                url=url,
                target="tunnel-esp32s3-v1",
                version=version,
                size=len(data),
                sha256=hashlib.sha256(data).hexdigest(),
                unix_time=int(time.time()),
            )
            request["signature"] = sign_request(request, key_path)
            payload = json.dumps(request, separators=(",", ":"))
            if len(payload) > 1536:
                raise ValueError("FOTA command too large")
            with self.condition:
                self.update_expected, self.update_ack = request, None
            try:
                if self.client.publish(self.root + "/ota/cmd", payload, qos=1, retain=False).rc:
                    raise UnknownOutcome("FOTA publish failed; query status before any new request")
                deadline = time.monotonic() + self.timeout
                with self.condition:
                    while self.update_ack is None:
                        left = deadline - time.monotonic()
                        if left <= 0 or not self.connected:
                            raise UnknownOutcome(
                                f"No FOTA ACK for job {job}; do not retry automatically"
                            )
                        self.condition.wait(left)
                    return dict(self.update_ack)  # accepted is not success
            finally:
                self.update_expected = None

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()
