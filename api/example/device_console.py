"""All 19 OPs in one persistent TCP/RTU/MQTT session. No automatic claim or Reset."""

import argparse
import json
import os
import shlex
import sys
import threading
import time

from tunnel_sdk import (
    BusyError,
    ProtocolError,
    TunnelDevice,
    UnknownOutcome,
    require_accepted,
)
from tunnel_sdk.common import named_words


# Console name, argument names, public SDK helper. Return loss takes kHz directly.
COMMANDS = {
    "reset": ((), "reset"),
    "gate": (("close",), "set_gate"),
    "belt": (("run",), "set_belt"),
    "scan": (("run",), "set_scan"),
    "mode": (("mode",), "set_mode"),
    "confirm": ((), "confirm"),
    "rescan": ((), "rescan"),
    "speed": (("level",), "set_speed"),
    "maintenance": (("enabled",), "set_maintenance"),
    "reader.info": ((), "get_reader_info"),
    "antenna.power.get": ((), "get_antenna_power"),
    "antenna.power.set": (
        ("ant1_dbm", "ant2_dbm", "ant3_dbm", "ant4_dbm", "ram_only"),
        "set_antenna_power",
    ),
    "antenna.mask.set": (("mask", "ram_only"), "set_antenna_mask"),
    "reader.profile.get": ((), "get_rf_profile"),
    "reader.profile.set": (("encoded",), "set_rf_profile"),
    "reader.gen2x.get": ((), "get_gen2x"),
    "reader.gen2x.set": (("value",), "set_gen2x"),
    "reader.ex10.get": ((), "get_ex10"),
    "antenna.return_loss": (("frequency_khz", "antenna"), "measure_return_loss"),
}
READER_COMMANDS = {
    "reader.info": 0x21,
    "antenna.power.get": 0x94,
    "antenna.power.set": 0x2F,
    "antenna.mask.set": 0x3F,
    "reader.profile.get": 0x7F,
    "reader.profile.set": 0x7F,
    "reader.gen2x.get": 0x7F,
    "reader.gen2x.set": 0x7F,
    "reader.ex10.get": 0xEC,
    "antenna.return_loss": 0x91,
}
CONTROL = {"claim", "heartbeat", "release", "stop", "emergency"}
READ_ONLY = {"status", "epc", "owner", "connected", "functions", "help", "quit"}


def parse_command(line):
    parts = shlex.split(line)
    if not parts:
        return "help", {}
    name, tokens = parts[0], parts[1:]
    if name in CONTROL | READ_ONLY:
        if tokens:
            raise ValueError(f"{name} takes no arguments")
        return name, {}
    if name not in COMMANDS:
        raise ValueError("Unknown command; enter help")
    names = COMMANDS[name][0]
    if len(tokens) != len(names):
        raise ValueError("Usage: " + name + " " + " ".join(names))
    params = dict(
        zip(names, [int(v, 16) if v.lower().startswith("0x") else int(v) for v in tokens])
    )
    if name == "antenna.return_loss":
        f, antenna = params["frequency_khz"], params["antenna"]
        if not 840000 <= f <= 960000 or (f % 100 and f % 125) or not 0 <= antenna <= 3:
            raise ValueError("frequency: 840000..960000 kHz on 100/125 kHz grid; antenna: 0..3")
    else:
        named_words(name, params)  # Reject invalid shape/range before any I/O.
    return name, params


def decode_reader(name, reply):
    data = reply["data"]
    minimum = {
        "reader.info": 9,
        "antenna.power.get": 4,
        "reader.profile.get": 1,
        "reader.gen2x.get": 2,
        "antenna.return_loss": 1,
    }.get(name, 0)
    if len(data) < minimum:
        raise ProtocolError(f"Short reader payload for {name}: {data}")
    if name == "reader.info":
        return {"antenna_mask": data[8] & 15, "raw_data": data}
    if name == "antenna.power.get":
        return dict(zip(("ANT1", "ANT2", "ANT3", "ANT4"), data[:4]))
    if name == "reader.profile.get":
        return {"profile": data[0], "raw_data": data}
    if name == "reader.gen2x.get":
        return {"gen2x": (data[0] << 8) | data[1]}
    if name == "antenna.return_loss":
        return {"return_loss_raw": data[0]}  # Reader-specific units, not invented dB.
    return {"raw_data": data}


class Console:
    """Example-owned heartbeat thread, only after an explicit successful claim.

    SDK itself has no automatic heartbeat/reconnect/retry. One lock serializes
    user commands, ownership checks and heartbeat. Failed renewal latches an
    error; it never reacquires control. Open a new console after diagnosing it.
    """

    def __init__(self, device, *, heartbeat_interval=0.5, reader_timeout=3.0):
        self.device = device
        self.interval = heartbeat_interval
        self.reader_timeout = reader_timeout
        self.claimed = False
        self.failure = None
        self.lock = threading.RLock()
        self.done = threading.Event()
        self.thread = None

    def _check(self):
        if self.failure is not None:
            raise ProtocolError(f"Session lost/uncertain; no automatic reclaim: {self.failure}")

    def _renew(self):
        with self.lock:
            if self.claimed and not self.done.is_set():
                self._check()
                if not self.device.has_control():
                    raise ProtocolError("Ownership lost")
                require_accepted(self.device.heartbeat())

    def _heartbeat_loop(self):
        while not self.done.wait(self.interval):
            try:
                self._renew()
            except Exception as exc:
                with self.lock:
                    self.failure = exc
                print(f"HEARTBEAT STOPPED: {exc}", file=sys.stderr)
                return

    def _wait_reader(self, name, ack):
        deadline = time.monotonic() + self.reader_timeout
        while time.monotonic() < deadline:
            with self.lock:
                self._check()
                if not self.device.has_control():
                    self.failure = ProtocolError("Ownership lost while waiting for reader")
                    self._check()
                reply = self.device.get_reader_result(
                    ack["sequence"],
                    expected_command=READER_COMMANDS[name],
                )
            if reply is not None:
                return {"reply": reply, "decoded": decode_reader(name, reply)}
            self.done.wait(0.05)
        raise UnknownOutcome("Reader reply deadline exceeded; inspect state, do not repeat Set")

    def run(self, name, params=None):
        params = params or {}
        d = self.device
        with self.lock:
            if name == "status":
                return d.get_status()
            if name == "epc":
                state = d.get_status()
                return {"epc_count": state["epc_count"], "total_reads": state["total_reads"]}
            if name == "owner":
                return {"has_control": d.has_control()}
            if name == "connected":
                return {"connected": d.is_connected()}
            if name == "functions":
                return d.get_functions()
            if name == "help":
                return {
                    "commands": sorted(CONTROL | READ_ONLY)
                    + [n + " " + " ".join(spec[0]) for n, spec in COMMANDS.items()]
                }
            if name in ("stop", "emergency", "release"):
                ack = require_accepted(getattr(d, name)())
                self.claimed = False  # Emergency may retain owner, but never renew implicitly.
                return ack
            self._check()
            if name == "claim":
                ack = require_accepted(d.claim())
                self.claimed = True
                if not d.has_control():
                    self.failure = ProtocolError("Claim ownership not confirmed")
                    self._check()
                if self.thread is None:
                    self.thread = threading.Thread(target=self._heartbeat_loop, daemon=True)
                    self.thread.start()
                return ack
            if name == "heartbeat":
                if not self.claimed or not d.has_control():
                    raise ProtocolError("No active local claim; heartbeat must not reclaim")
                return require_accepted(d.heartbeat())
            if name not in COMMANDS:
                raise ValueError("Unknown command")
            if not self.claimed:
                raise ProtocolError("Use claim explicitly first (STOP/HOLD)")
            if not d.has_control():
                self.failure = ProtocolError("Ownership lost")
                self._check()
            ack = require_accepted(getattr(d, COMMANDS[name][1])(**params))
        result = {"ack": ack, "note": "Accepted; inspect status for machine completion"}
        if name in READER_COMMANDS:
            result = {"ack": ack, **self._wait_reader(name, ack)}
            readback = {
                "antenna.power.set": "antenna.power.get",
                "antenna.mask.set": "reader.info",
                "reader.profile.set": "reader.profile.get",
                "reader.gen2x.set": "reader.gen2x.get",
            }.get(name)
            if readback:
                result["readback"] = self.run(readback)["decoded"]
                value = result["readback"]
                if name == "antenna.power.set":
                    matches = list(value.values()) == [params[f"ant{i}_dbm"] for i in range(1, 5)]
                elif name == "antenna.mask.set":
                    matches = value["antenna_mask"] == params["mask"]
                elif name == "reader.gen2x.set":
                    matches = value["gen2x"] == params["value"]
                else:
                    matches = (value["profile"] & 63) == (params["encoded"] & 63)
                if not matches:
                    raise ProtocolError(f"Set ACK received but readback mismatch: {result}")
                result["verified"] = True
        return result

    def close(self):
        self.done.set()
        if self.thread is not None:
            self.thread.join()
        # Explicit example policy on exit: release owned control (STOP/HOLD).
        # Even after uncertain ACK, check ownership; never steal another owner.
        if self.device.has_control():
            require_accepted(self.device.release())
        self.claimed = False


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--transport", choices=["tcp", "rtu", "mqtt"], required=True)
    p.add_argument("--host")
    p.add_argument("--serial-port")
    p.add_argument("--port", type=int)
    p.add_argument("--unit", type=int, default=1)
    p.add_argument("--baud", type=int, default=19200)
    p.add_argument("--parity", choices=["N", "E", "O"], default="E")
    p.add_argument("--stop-bits", type=int, choices=[1, 2], default=1)
    p.add_argument("--root", default="tunnel/01/v1")
    p.add_argument(
        "--command",
        action="append",
        help="Run specified commands in ONE session; stop at first error",
    )
    return p


def make_device(p, a):
    if a.transport == "rtu":
        if not a.serial_port:
            p.error("RTU requires --serial-port")
        return TunnelDevice.rtu(
            a.serial_port, baud=a.baud, parity=a.parity, stop_bits=a.stop_bits, unit=a.unit
        )
    if not a.host:
        p.error("TCP/MQTT requires --host")
    if a.transport == "tcp":
        return TunnelDevice.tcp(a.host, port=a.port or 502, unit=a.unit)
    return TunnelDevice.mqtt(
        a.host,
        port=a.port or 1883,
        root=a.root,
        username=os.getenv("TUNNEL_MQTT_USERNAME", ""),
        password=os.getenv("TUNNEL_MQTT_PASSWORD", ""),
    )


def main():
    p = parser()
    a = p.parse_args()
    try:
        commands = [parse_command(line) for line in a.command] if a.command else None
    except ValueError as exc:
        p.error(str(exc))
    failed = False
    with make_device(p, a) as device:
        console = Console(device)
        try:
            if commands is not None:
                for name, params in commands:
                    if name == "quit":
                        break
                    print(json.dumps(console.run(name, params), ensure_ascii=False))
            else:
                print(
                    "Connected. No claim yet. Enter help. Claim and exit release cause STOP/HOLD."
                )
                while True:
                    try:
                        name, params = parse_command(input("tunnel> "))
                        if name == "quit":
                            break
                        print(json.dumps(console.run(name, params), ensure_ascii=False))
                    except BusyError as exc:
                        print(f"BUSY: {exc}. No retry or session takeover; inspect status.")
                    except UnknownOutcome:
                        raise  # End session; do not let uncertain Set be repeated accidentally.
                    except (ValueError, ProtocolError) as exc:
                        print(f"REJECTED: {exc}")
                    except EOFError:
                        break
        except (ProtocolError, KeyboardInterrupt) as exc:
            failed = True
            print(f"Stopped: {exc}", file=sys.stderr)
        finally:
            try:
                console.close()
            except Exception as exc:
                failed = True
                print(
                    f"Release not confirmed: {exc}; inspect state. Heartbeat has stopped.",
                    file=sys.stderr,
                )
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
