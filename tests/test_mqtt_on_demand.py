"""Local broker + production EspMqtt/MQTT/Modbus code with simulated ESP/hardware IO."""

import asyncio
import importlib.util
import json
import logging
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
from amqtt.broker import Broker
import paho.mqtt.client as mqtt

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "api"))
spec = importlib.util.spec_from_file_location("tunnel_api", ROOT / "api/tunnel_api.py")
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)
from tunnel_sdk import BusyError, TunnelDevice

sys.path.insert(0, str(ROOT / "api/example"))
from reader_power import read_power
from device_console import Console

logging.disable(logging.CRITICAL)
loop = asyncio.new_event_loop()
thread = threading.Thread(target=loop.run_forever, daemon=True)
thread.start()
with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]


async def start():
    b = Broker(
        {
            "listeners": {"default": {"type": "tcp", "bind": f"127.0.0.1:{port}"}},
            "plugins": {
                "amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}
            },
        }
    )
    await b.start()
    return b


broker = asyncio.run_coroutine_threadsafe(start(), loop).result(10)
native = subprocess.Popen(
    [str(ROOT / "test_build/mqtt_transport/mqtt_transport.exe"), "--serve"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    text=True,
    bufsize=1,
)
lock = threading.RLock()
stop = threading.Event()
subscribed = threading.Event()
started = time.monotonic()
commands = []
reads = []
states = []
failures = []
emitted_states = []
poison = False
bridge = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="device-simulator")
observer = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="observer")


def line(message):
    native.stdin.write(message + "\n")
    native.stdin.flush()
    result = native.stdout.readline()
    if not result:
        raise RuntimeError("Native simulator stopped")
    return json.loads(result)


def publish(messages):
    for msg in messages:
        payload = msg["payload"]
        if msg["topic"].endswith("/state"):
            emitted_states.append(payload)
        if msg["topic"].endswith("/state") and poison:
            # Only wrong correlation IDs are published. SDK must not accept these.
            payload = dict(payload, request_client="0000000000000000")
            bridge.publish(msg["topic"], json.dumps(payload), qos=1)
            payload = dict(payload, request_client=msg["payload"]["request_client"], request_id=0)
        bridge.publish(
            msg["topic"], json.dumps(payload), qos=msg["qos"], retain=bool(msg["retain"])
        )


def call(message):
    with lock:
        publish(line("T " + str(int((time.monotonic() - started) * 1000))))
        result = line(message)
        if isinstance(result, list):
            publish(result)
        return result


def received(client, userdata, msg):
    try:
        if msg.topic.endswith("/cmd"):
            commands.append(json.loads(msg.payload))
            call("C " + msg.payload.decode())
        elif msg.topic.endswith("/get"):
            reads.append(json.loads(msg.payload))
            call(("R " if msg.retain else "G ") + msg.payload.decode())
    except Exception as exc:
        failures.append(repr(exc))


bridge.on_connect = lambda c, u, f, r, p: c.subscribe(
    [("test/tunnel/get", 1), ("test/tunnel/cmd", 1)]
)
bridge.on_subscribe = lambda *args: subscribed.set()
bridge.on_message = received
observer.on_connect = lambda c, u, f, r, p: c.subscribe("test/tunnel/state", 1)
observer.on_message = lambda c, u, m: states.append(json.loads(m.payload))
bridge.connect("127.0.0.1", port)
bridge.loop_start()
observer.connect("127.0.0.1", port)
observer.loop_start()
assert subscribed.wait(3)


def ticking():
    try:
        while not stop.wait(0.02):
            with lock:
                publish(line("T " + str(int((time.monotonic() - started) * 1000))))
    except Exception as exc:
        failures.append(repr(exc))


ticker = threading.Thread(target=ticking, daemon=True)
ticker.start()
devices = []
try:
    time.sleep(0.6)
    assert not states and not reads and not commands, "Subscribe triggered a state message"
    a = sdk.Mqtt("127.0.0.1", port, root="test/tunnel", timeout=1)
    devices.append(a)
    b = sdk.Mqtt("127.0.0.1", port, root="test/tunnel", timeout=1)
    devices.append(b)
    assert not reads, "SDK constructor must not poll"
    answers = {}

    def read_into(name, dev):
        answers[name] = dev.status()

    jobs = [
        threading.Thread(target=read_into, args=("a", a)),
        threading.Thread(target=read_into, args=("b", b)),
    ]
    for j in jobs:
        j.start()
    for j in jobs:
        j.join(3)
    assert set(answers) == {"a", "b"} and all(
        s["unique_epcs"] == 7 and s["firmware"] == 1120 for s in answers.values()
    )
    assert a.state["epc_count"] == 7 and a.state["machine_state"] == a.state["registers"][2]
    assert a.state["request_client"] == a.client_id and b.state["request_client"] == b.client_id
    assert a.state["request_id"] == 1 and b.state["request_id"] == 1
    count = len(emitted_states)
    read_count = len(reads)
    time.sleep(0.4)
    assert len(emitted_states) == count and len(reads) == read_count and commands == []
    poison = True
    try:
        a.status()
        raise AssertionError("Accepted someone else or old request reply")
    except sdk.ProtocolError as exc:
        assert "No matching" in str(exc)
    finally:
        poison = False
    time.sleep(1.6)  # Cached state is stale: _send must fetch a new token first.
    assert a.claim()["accepted"]
    assert a.status()["owner"] == 6
    try:
        b.claim()
        raise AssertionError("Foreign MQTT client stole active session")
    except BusyError as exc:
        assert exc.code == 5
    assert a.has_control()
    assert a.command("maintenance", [1])["accepted"]
    ack = a.call("antenna.power.get")
    assert ack["accepted"]
    assert commands[-1]["method"] == "antenna.power.get" and commands[-1]["params"] == {}
    deadline = time.monotonic() + 2
    beat_at = time.monotonic() + 0.4
    while True:
        s = a.status()
        if s["reader_state"] == 2 and s["reader_sequence"] == ack["sequence"]:
            break
        assert time.monotonic() < deadline
        if time.monotonic() >= beat_at:
            a.heartbeat()
            beat_at = time.monotonic() + 0.4
        time.sleep(0.03)
    assert s["reader_reply"][:6] == [148, 0, 30, 30, 30, 30]
    assert a.command("maintenance", [0])["accepted"] and a.release()["accepted"]
    assert a.status()["owner"] == 0
    # Read requests from another application must not hold/renew control.
    baseline = len(commands)
    with TunnelDevice.mqtt("127.0.0.1", port=port, root="test/tunnel") as public:
        assert public.get_status()["epc_count"] == 7
        assert read_power(public) == dict.fromkeys(["ANT1", "ANT2", "ANT3", "ANT4"], 30)
        assert not public.has_control()
        console = Console(public)
        try:
            console.run("claim")
            console.run("maintenance", {"enabled": 1})
            assert console.run("antenna.power.get")["decoded"] == dict.fromkeys(
                ["ANT1", "ANT2", "ANT3", "ANT4"],
                30,
            )
        finally:
            console.close()
        assert not public.has_control()
    assert all("method" not in c for c in commands[baseline:])
    assert any(c.get("op") == 21 for c in commands[baseline:])
    print("PASS public MQTT API uses numeric 1.11-compatible OPs and reader cleanup", flush=True)

    assert a.claim()["accepted"]
    deadline = time.monotonic() + 3.2
    while time.monotonic() < deadline:
        b.status()
        time.sleep(0.07)
    assert a.status()["owner"] == 0
    # Connection reset must yield a new session without pushing any unsolicited state.
    old_session = a.status()["session"]
    count = len(emitted_states)
    call("X")
    time.sleep(0.2)
    assert len(emitted_states) == count
    assert a.status()["session"] != old_session
    with tempfile.TemporaryDirectory() as temp:
        cfg = pathlib.Path(temp) / "config.json"
        cfg.write_text(
            json.dumps({"mqtt_host": "127.0.0.1", "mqtt_port": port, "mqtt_root": "test/tunnel"})
        )
        baseline = len(commands)
        demo = subprocess.run(
            [
                sys.executable,
                str(ROOT / "api/mqtt_demo.py"),
                "--config",
                str(cfg),
                "--samples",
                "3",
            ],
            capture_output=True,
            text=True,
            timeout=12,
        )
        assert demo.returncode == 0, (demo.stdout, demo.stderr)
        assert len(demo.stdout.strip().splitlines()) == 3 and len(commands) == baseline
        success = subprocess.run(
            [sys.executable, str(ROOT / "api/mqtt_read_power.py"), "--config", str(cfg)],
            capture_output=True,
            text=True,
            timeout=12,
        )
        assert success.returncode == 0, (success.stdout, success.stderr)
        assert json.loads(success.stdout)["power_dbm"] == dict.fromkeys(
            ["ANT1", "ANT2", "ANT3", "ANT4"], 30
        )
        assert any(c["action"] == "heartbeat" for c in commands[baseline:])
        assert a.status()["owner"] == 0
        call("E 1")
        rejected = subprocess.run(
            [sys.executable, str(ROOT / "api/mqtt_read_power.py"), "--config", str(cfg)],
            capture_output=True,
            text=True,
            timeout=12,
        )
        assert (
            rejected.returncode != 0
            and "Reader rejected" in rejected.stderr
            and not rejected.stdout
        )
        assert a.status()["owner"] == 0
    assert not failures, failures
    result = dict(
        no_unsolicited_state=True,
        two_clients_correlated=True,
        wrong_reply_rejected=True,
        fresh_token_before_commands=True,
        reads_do_not_renew_lease=True,
        reconnect_bootstrap=True,
        read_only_demo_samples=3,
        reader_power_test=True,
        heartbeat_test=True,
        reader_error_test=True,
        requests_observed=len(reads),
        control_messages_observed=len(commands),
    )
    (ROOT / "test_build/mqtt_on_demand.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
finally:
    for d in devices:
        d.close()
    stop.set()
    ticker.join(2)
    bridge.disconnect()
    bridge.loop_stop()
    observer.disconnect()
    observer.loop_stop()
    native.stdin.close()
    native.wait(timeout=3)
    asyncio.run_coroutine_threadsafe(broker.shutdown(), loop).result(5)
    loop.call_soon_threadsafe(loop.stop)
    thread.join(2)
