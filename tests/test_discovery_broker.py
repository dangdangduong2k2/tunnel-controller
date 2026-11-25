"""Real local MQTT broker integration; simulated boards, never public broker."""

import asyncio
import json
import logging
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time
import unittest

from amqtt.broker import Broker
import paho.mqtt.client as mqtt

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "fota"))
from portal import create_app


def wait_for(condition):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.05)
    raise AssertionError("MQTT condition timed out")


class BrokerTest(unittest.TestCase):
    def test_retained_detection_live_heartbeat_lwt_reconnect(self):
        logging.getLogger("amqtt").setLevel(logging.ERROR)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        loop = asyncio.new_event_loop()
        worker = threading.Thread(target=loop.run_forever, daemon=True)
        worker.start()

        async def start():
            b = Broker(
                {
                    "listeners": {"default": {"type": "tcp", "bind": f"127.0.0.1:{port}"}},
                    "plugins": {
                        "amqtt.plugins.authentication.AnonymousAuthPlugin": {
                            "allow_anonymous": True
                        }
                    },
                }
            )
            await b.start()
            return b

        broker = asyncio.run_coroutine_threadsafe(start(), loop).result(10)
        publisher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="simulated-board")
        publisher.will_set("test/board/v1/availability", '{"online":false}', qos=1, retain=True)
        discovery = None
        try:
            publisher.connect("127.0.0.1", port, keepalive=5)
            publisher.loop_start()
            payload = json.dumps(
                {
                    "discovery_version": 1,
                    "device_id": "112233445566",
                    "model": "tunnel-esp32s3-v1",
                    "root": "test/board/v1",
                    "firmware": 1130,
                }
            )
            topic = "tunnel/discovery/v1/112233445566"
            publisher.publish(topic, payload, qos=1, retain=True).wait_for_publish(3)
            with tempfile.TemporaryDirectory() as temp:
                app = create_app(
                    temp,
                    config={
                        "BROKER_HOST": "127.0.0.1",
                        "BROKER_PORT": port,
                        "DISCOVERY_START": False,
                    },
                )
                discovery = app.extensions["discovery"]
                discovery.start()
                wait_for(lambda: discovery.connected and len(discovery.devices()) == 1)
                self.assertFalse(
                    discovery.devices()[0]["online"], "Retained discovery must not assert Online"
                )
                publisher.publish(topic, payload, qos=1, retain=True).wait_for_publish(3)
                wait_for(lambda: discovery.devices()[0]["online"])
                self.assertEqual(
                    app.test_client()
                    .get("/tunnel/admin/api/overview")
                    .json["devices"][0]["device_id"],
                    "112233445566",
                )
                # Terminate TCP without MQTT DISCONNECT so the broker really emits LWT.
                publisher.loop_stop()
                publisher.socket().shutdown(socket.SHUT_RDWR)
                publisher.socket().close()
                wait_for(lambda: not discovery.devices()[0]["online"])
                # Clean receiver restart sees retained identity but remains non-live.
                discovery.stop()
                app2 = create_app(
                    temp,
                    config={
                        "BROKER_HOST": "127.0.0.1",
                        "BROKER_PORT": port,
                        "DISCOVERY_START": False,
                    },
                )
                discovery = app2.extensions["discovery"]
                discovery.start()
                wait_for(lambda: discovery.connected and len(discovery.devices()) == 1)
                self.assertFalse(discovery.devices()[0]["online"])
                discovery.stop()
                discovery = None
        finally:
            if discovery:
                discovery.stop()
            publisher.disconnect()
            publisher.loop_stop()
            asyncio.run_coroutine_threadsafe(broker.shutdown(), loop).result(10)
            loop.call_soon_threadsafe(loop.stop)
            worker.join(5)
            loop.close()


if __name__ == "__main__":
    unittest.main()
