"""Claim, maintenance, read power, release. Claim STOP/HOLDs the machine."""

import argparse
import json
import os
import sys
import time
from tunnel_sdk import TunnelDevice, ProtocolError


def accepted(ack):
    if not ack.get("accepted"):
        raise ProtocolError(f"Device rejected command: {ack}")
    return ack


def read_power(device):
    state = device.get_status()
    if state["owner"] or state["activity_flags"] & 15 or state["reader_state"] == 1:
        raise ProtocolError("Requires idle, unowned machine and no pending reader command")
    claimed = False
    try:
        accepted(device.claim())
        claimed = True
        if not device.has_control():
            raise ProtocolError("Ownership not confirmed")
        accepted(device.heartbeat())
        accepted(device.set_maintenance(1))
        accepted(device.heartbeat())
        ack = accepted(device.get_antenna_power())
        deadline = time.monotonic() + 3.0
        next_heartbeat = time.monotonic()
        while True:
            now = time.monotonic()
            if now >= deadline:
                raise ProtocolError("Reader reply timeout; inspect live state")
            if now >= next_heartbeat:
                if not device.has_control():
                    raise ProtocolError("Ownership lost")
                accepted(device.heartbeat())
                next_heartbeat = time.monotonic() + 0.5
            reply = device.get_reader_result(ack["sequence"], expected_command=0x94)
            if reply is not None:
                if len(reply["data"]) != 4:
                    raise ProtocolError("Expected four antenna powers")
                return dict(zip(["ANT1", "ANT2", "ANT3", "ANT4"], reply["data"]))
            time.sleep(0.05)
    finally:
        if claimed:
            try:
                if device.has_control():
                    if device.get_status()["reader_state"] != 1:
                        accepted(device.set_maintenance(0))
                    accepted(device.release())
            except Exception as exc:
                print(
                    f"Cleanup not confirmed: {exc}. Inspect device; lease expires without heartbeat.",
                    file=sys.stderr,
                )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--transport", choices=["tcp", "rtu", "mqtt"], required=True)
    p.add_argument("--host")
    p.add_argument("--serial-port")
    p.add_argument("--baud", type=int, default=19200)
    p.add_argument("--parity", choices=["N", "E", "O"], default="E")
    p.add_argument("--stop-bits", type=int, choices=[1, 2], default=1)
    p.add_argument("--unit", type=int, default=1)
    p.add_argument("--port", type=int)
    p.add_argument("--root", default="tunnel/01/v1")
    a = p.parse_args()
    if a.transport == "rtu":
        if not a.serial_port:
            p.error("--serial-port is required for RTU")
        device = TunnelDevice.rtu(
            a.serial_port, baud=a.baud, parity=a.parity, stop_bits=a.stop_bits, unit=a.unit
        )
    else:
        if not a.host:
            p.error("--host is required")
        device = (
            TunnelDevice.tcp(a.host, port=a.port or 502, unit=a.unit)
            if a.transport == "tcp"
            else TunnelDevice.mqtt(
                a.host,
                port=a.port or 1883,
                root=a.root,
                username=os.getenv("TUNNEL_MQTT_USERNAME", ""),
                password=os.getenv("TUNNEL_MQTT_PASSWORD", ""),
            )
        )
    with device:
        print(json.dumps(read_power(device)))


if __name__ == "__main__":
    main()
