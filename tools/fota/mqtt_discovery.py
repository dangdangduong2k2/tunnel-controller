"""Discover only boards that actually announce themselves; never seed a device."""

from contextlib import closing
import json
import re
import threading
import time
import uuid

TOPIC = "tunnel/discovery/v1/"
TTL = 45


class Discovery:
    def __init__(self, db, config, clock=time.time):
        self.db, self.config, self.clock = db, config, clock
        self.scope = f"{config['BROKER_HOST']}:{config['BROKER_PORT']}"
        self.connected = False
        self.state = "connecting"
        self.client = None
        self.lock = threading.RLock()
        self.subscriptions = set()
        with closing(db()) as c, c:
            c.execute("""CREATE TABLE IF NOT EXISTS devices (
                broker TEXT, device_id TEXT, root TEXT, firmware INTEGER,
                last_seen REAL, last_live REAL, available INTEGER,
                PRIMARY KEY(broker,device_id))""")
            # A saved record or retained message is not evidence of a current connection.
            c.execute("UPDATE devices SET last_live=NULL,available=0 WHERE broker=?", (self.scope,))

    def start(self):
        import paho.mqtt.client as mqtt

        self.client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2, client_id="tunnel-discovery-" + uuid.uuid4().hex[:16]
        )
        if self.config["BROKER_USER"]:
            self.client.username_pw_set(self.config["BROKER_USER"], self.config["BROKER_PASSWORD"])
        self.client.on_connect = self._connect
        self.client.on_disconnect = self._disconnect
        self.client.on_message = self._message
        self.client.on_subscribe = self._subscribe
        self.client.reconnect_delay_set(1, 15)
        self.client.connect_async(self.config["BROKER_HOST"], self.config["BROKER_PORT"], 15)
        self.client.loop_start()

    def stop(self):
        if self.client:
            self.client.disconnect()
            self.client.loop_stop()

    def _connect(self, client, userdata, flags, reason, properties):
        self.connected = False
        self.subscriptions.clear()
        if reason == 0:
            self.state = "subscribing"
            _, self.discovery_mid = client.subscribe(TOPIC + "+", qos=1)
            with closing(self.db()) as c:
                roots = [
                    r[0]
                    for r in c.execute(
                        "SELECT DISTINCT root FROM devices WHERE broker=?", (self.scope,)
                    )
                ]
            for root in roots:
                self._watch(root)
        else:
            self.state = "connection_refused"

    def _subscribe(self, client, userdata, mid, reasons, properties):
        if mid == getattr(self, "discovery_mid", None):
            self.connected = bool(reasons) and all(not r.is_failure for r in reasons)
            self.state = "listening" if self.connected else "subscription_refused"

    def _disconnect(self, *args):
        self.connected = False
        self.state = "disconnected"
        with closing(self.db()) as c, c:
            c.execute("UPDATE devices SET last_live=NULL,available=0 WHERE broker=?", (self.scope,))

    def _watch(self, root):
        if self.client and root not in self.subscriptions:
            self.client.subscribe(root + "/availability", qos=1)
            self.subscriptions.add(root)

    def _message(self, client, userdata, message):
        self.receive(message.topic, message.payload, message.retain)

    def receive(self, topic, payload, retained=False):
        if len(payload) > 512:
            return False
        try:
            obj = json.loads(payload)
            if not isinstance(obj, dict):
                return False
            if topic.startswith(TOPIC):
                device = topic[len(TOPIC) :]
                root, version = obj.get("root"), obj.get("firmware")
                if (
                    not re.fullmatch(r"[0-9a-f]{12}", device)
                    or obj.get("device_id") != device
                    or type(obj.get("discovery_version")) is not int
                    or obj["discovery_version"] != 1
                    or obj.get("model") != "tunnel-esp32s3-v1"
                    or not isinstance(root, str)
                    or not 1 <= len(root) <= 64
                    or not re.fullmatch(r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*", root)
                    or type(version) is not int
                    or not 1 <= version <= 0xFFFFFFFF
                ):
                    return False
                now = self.clock()
                with self.lock, closing(self.db()) as c, c:
                    count = c.execute(
                        "SELECT COUNT(*) FROM devices WHERE broker=?", (self.scope,)
                    ).fetchone()[0]
                    old = c.execute(
                        "SELECT * FROM devices WHERE broker=? AND device_id=?", (self.scope, device)
                    ).fetchone()
                    if not old and count >= 256:
                        return False
                    # Never let stale retained delivery replace fresh metadata/live status.
                    if retained and old and old["last_live"] is not None:
                        return False
                    c.execute(
                        """INSERT INTO devices VALUES(?,?,?,?,?,?,?)
                        ON CONFLICT(broker,device_id) DO UPDATE SET
                        root=excluded.root,firmware=excluded.firmware,last_seen=excluded.last_seen,
                        last_live=excluded.last_live,available=excluded.available""",
                        (
                            self.scope,
                            device,
                            root,
                            version,
                            now,
                            None if retained else now,
                            0 if retained else 1,
                        ),
                    )
                self._watch(root)
                return True
            if topic.endswith("/availability") and type(obj.get("online")) is bool:
                # Retained online is not a heartbeat. Offline LWT is useful immediately.
                if not obj["online"]:
                    with closing(self.db()) as c, c:
                        c.execute(
                            "UPDATE devices SET available=0,last_live=NULL WHERE broker=? AND root=?",
                            (self.scope, topic[: -len("/availability")]),
                        )
                    return True
        except (ValueError, TypeError, UnicodeError):
            pass
        return False

    def devices(self):
        with closing(self.db()) as c:
            rows = [
                dict(r)
                for r in c.execute(
                    "SELECT * FROM devices WHERE broker=? ORDER BY device_id", (self.scope,)
                )
            ]
        roots = [r["root"] for r in rows]
        for r in rows:
            r["name"] = "Tunnel · " + r["device_id"]
            r["conflict"] = roots.count(r["root"]) > 1
            r["online"] = bool(
                self.connected
                and r["available"]
                and r["last_live"] is not None
                and 0 <= self.clock() - r["last_live"] <= TTL
            )
            r.pop("broker")
        return rows

    def select(self, device_id, require_live=False):
        device = next((d for d in self.devices() if d["device_id"] == device_id), None)
        if not device:
            raise ValueError("Chọn một bo đã được phát hiện qua MQTT.")
        if device["conflict"]:
            raise ValueError("Hai bo đang dùng cùng topic root. Đổi root riêng cho mỗi bo trước.")
        if require_live and not device["online"]:
            raise ValueError("Bo chưa có heartbeat mới. Chờ bo Online trước khi cập nhật.")
        return device
