import json
import threading
import time
import uuid
from .common import Commands, words, named_words
from .operations import OPERATIONS
from .errors import ProtocolError, UnknownOutcome
from .status import decode_status


class Mqtt(Commands):
    def __init__(self, host, port=1883, username="", password="", root="tunnel/01/v1", timeout=3):
        import paho.mqtt.client as mqtt

        if not host:
            raise ValueError("Configure mqtt_host first")
        self.root, self.timeout = root.rstrip("/"), timeout
        self.client_id, self.sequence = uuid.uuid4().hex[:16], 0
        self.lock, self.condition = threading.RLock(), threading.Condition()
        self.state, self.received, self.pending, self.connected = None, 0, None, False
        self.read_sequence, self.read_pending, self.subscribed = 0, None, False
        self.client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id="tunnel-sdk-" + self.client_id,
            clean_session=True,
            protocol=mqtt.MQTTv311,
            reconnect_on_failure=False,
        )
        if username:
            self.client.username_pw_set(username, password)
        self.client.on_connect, self.client.on_disconnect = self._connect, self._disconnect
        self.client.on_message = self._message
        self.client.on_subscribe = self._subscribe
        self.client.connect(host, port, keepalive=5)
        self.client.loop_start()
        try:
            deadline = time.monotonic() + timeout
            with self.condition:
                while not self.subscribed:
                    left = deadline - time.monotonic()
                    if left <= 0:
                        raise ProtocolError(
                            "MQTT subscriptions not ready; check broker and topic permissions"
                        )
                    self.condition.wait(left)
        except Exception:
            self.close()
            raise

    def _connect(self, client, userdata, flags, reason, properties):
        with self.condition:
            self.connected = reason == 0
            self.state = None
            self.subscribed = False
        if reason == 0:
            client.subscribe([(self.root + "/state", 1), (self.root + "/ack", 1)])

    def _subscribe(self, client, userdata, mid, reasons, properties):
        with self.condition:
            self.subscribed = len(reasons) == 2 and all(not r.is_failure for r in reasons)
            self.condition.notify_all()

    def _disconnect(self, *unused):
        with self.condition:
            self.connected = False
            self.subscribed = False
            self.state = None
            self.condition.notify_all()

    def _fresh(self):
        return self.connected and self.state is not None and time.monotonic() - self.received < 1.5

    def _message(self, client, userdata, message):
        if message.retain or len(message.payload) > 4096:
            return
        try:
            data = json.loads(message.payload)
            if not isinstance(data, dict) or data.get("api_version") != 1:
                return
            with self.condition:
                if message.topic == self.root + "/state":
                    if (
                        self.read_pending is None
                        or data.get("request_client") != self.client_id
                        or type(data.get("request_id")) is not int
                        or data["request_id"] != self.read_pending[0]
                    ):
                        return
                    decode_status(data["registers"])
                    session = data["session"]
                    owner = data["owner_client"]
                    if (
                        not isinstance(session, str)
                        or len(session) != 16
                        or any(c not in "0123456789abcdef" for c in session)
                        or not isinstance(owner, str)
                        or (
                            owner
                            and (
                                len(owner) != 16 or any(c not in "0123456789abcdef" for c in owner)
                            )
                        )
                    ):
                        return
                    if any(
                        type(data[k]) is not int or not 0 <= data[k] <= 0xFFFFFFFF
                        for k in ("epoch", "ms")
                    ):
                        return
                    if self.state and self.state["session"] == session:
                        delta = (data["ms"] - self.state["ms"]) & 0xFFFFFFFF
                        if delta >= 0x80000000:
                            return
                    self.state, self.received = data, time.monotonic()
                    self.read_pending = self.read_pending[0], data
                elif message.topic == self.root + "/ack" and self.pending:
                    expected, _ = self.pending
                    if all(
                        data.get(k) == expected[k] for k in ("session", "epoch", "client", "id")
                    ):
                        self.pending = expected, data
                self.condition.notify_all()
        except (ValueError, TypeError, KeyError, ProtocolError):
            return

    def status(self):
        with self.lock:
            with self.condition:
                if not self.connected or not self.subscribed:
                    raise ProtocolError("MQTT offline or subscriptions not ready")
                self.read_sequence += 1
                if self.read_sequence > 0xFFFFFFFF:
                    raise ProtocolError("Read ID exhausted; create a new client")
                request = dict(client=self.client_id, id=self.read_sequence)
                self.read_pending = self.read_sequence, None
            try:
                sent = self.client.publish(
                    self.root + "/get",
                    json.dumps(request, separators=(",", ":")),
                    qos=1,
                    retain=False,
                )
                if sent.rc != 0:
                    raise ProtocolError("MQTT read request publish failed")
                deadline = time.monotonic() + self.timeout
                with self.condition:
                    while self.read_pending[1] is None:
                        left = deadline - time.monotonic()
                        if left <= 0 or not self.connected:
                            raise ProtocolError(
                                "No matching /state reply; requires Tunnel 1.11.0 and /get publish permission"
                            )
                        self.condition.wait(left)
                    data = self.read_pending[1]
                    result = decode_status(data["registers"])
                    result.update(
                        session=data["session"],
                        epoch=data["epoch"],
                        owner_client=data["owner_client"],
                    )
                    return result
            finally:
                with self.condition:
                    self.read_pending = None

    def _send(self, action, op=None, args=(), method=None, params=None):
        with self.lock:
            self.status()  # Fresh token for every control request, without renewing the lease.
            with self.condition:
                if not self._fresh():
                    raise ProtocolError("No fresh MQTT state")
                self.sequence += 1
                if self.sequence > 0xFFFFFFFF:
                    raise ProtocolError("Client ID exhausted; create a new client")
                request = dict(
                    session=self.state["session"],
                    epoch=self.state["epoch"],
                    client=self.client_id,
                    id=self.sequence,
                    seen_ms=self.state["ms"],
                    action=action,
                )
                if op is not None:
                    request.update(op=op, args=words(args))
                if method is not None:
                    request.pop("action")
                    request.update(method=method, params=params)
                self.pending = request, None
            try:
                sent = self.client.publish(
                    self.root + "/cmd",
                    json.dumps(request, separators=(",", ":")),
                    qos=1,
                    retain=False,
                )
                if sent.rc != 0:
                    raise UnknownOutcome("MQTT publish failed; do not retry motion automatically")
                deadline = time.monotonic() + self.timeout
                with self.condition:
                    while self.pending[1] is None:
                        left = deadline - time.monotonic()
                        if left <= 0 or not self.connected:
                            raise UnknownOutcome(
                                "No device ACK; broker delivery is not machine acceptance"
                            )
                        self.condition.wait(left)
                    ack = self.pending[1]
                if ack["code"]:
                    raise ProtocolError(
                        f"MQTT code {ack['code']}, Modbus exception {ack['exception']}"
                    )
                ack["accepted"] = ack["exception"] == 0 and ack["result"] == 0
                if action == "claim" and ack["accepted"]:
                    try:
                        state = self.status()
                    except ProtocolError as exc:
                        raise UnknownOutcome(
                            "Claim ACK received but ownership could not be read"
                        ) from exc
                    if state["owner_client"] != self.client_id or state["owner"] != 6:
                        raise UnknownOutcome("Claim ACK received but ownership is not confirmed")
                return ack
            finally:
                with self.condition:
                    self.pending = None

    def claim(self):
        return self._send("claim")

    def has_control(self):
        state = self.status()
        return state["owner"] == 6 and state["owner_client"] == self.client_id

    def heartbeat(self):
        return self._send("heartbeat")

    def release(self):
        return self._send("release")

    def stop(self):
        return self._send("stop")

    def emergency(self):
        return self._send("emergency")

    def call(self, method, **params):
        named_words(method, params)  # Validate before touching the network.
        return self._send("execute", method=method, params=params)

    def execute(self, op, args=()):
        if type(op) is not int or op not in OPERATIONS.values():
            raise ValueError("Unsupported operation")
        return self._send("execute", op, args)

    def close(self):
        self.client.disconnect()
        self.client.loop_stop()
        # No hidden command on close. MQTT ownership expires <=3 s after last heartbeat.
