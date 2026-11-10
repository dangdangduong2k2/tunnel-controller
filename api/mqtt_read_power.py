"""Claim (STOP/HOLD), enter maintenance, read ANT1..4 power, then release.
No RF configuration write or motion command is sent. Requires an idle machine.
"""

import argparse
import json
import sys
import time
from tunnel_api import Mqtt, ProtocolError


def accepted(ack):
    if not ack.get("accepted"):
        raise ProtocolError("Device rejected command: " + json.dumps(ack))
    return ack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="mqtt_config.json")
    options = parser.parse_args()
    with open(options.config, encoding="utf-8-sig") as handle:
        cfg = json.load(handle)
    if not cfg.get("mqtt_host") or cfg["mqtt_host"] == "CHANGE_ME_BROKER":
        parser.error("Fill mqtt_host first")
    device = Mqtt(
        cfg["mqtt_host"],
        cfg.get("mqtt_port", 1883),
        username=cfg.get("mqtt_username", ""),
        password=cfg.get("mqtt_password", ""),
        root=cfg.get("mqtt_root", "tunnel/01/v1"),
        timeout=1.0,
    )
    claimed = False
    completed = False
    try:
        state = device.status()
        if state["owner"] != 0 or state["activity_flags"] & 15 or state["reader_state"] == 1:
            raise ProtocolError("Machine must be idle, unowned, with no pending reader command")
        print("Claim will STOP/HOLD the machine; reading antenna power only.", file=sys.stderr)
        accepted(device.claim())
        claimed = True
        accepted(device.command("maintenance", [1]))
        ack = accepted(device.command("antenna.power.get"))
        deadline = time.monotonic() + 3.0
        next_heartbeat = time.monotonic() + 0.5
        while True:
            state = device.status()
            if state["owner"] != 6 or state["owner_client"] != device.client_id:
                raise ProtocolError("MQTT ownership lost")
            if state["reader_sequence"] == ack["sequence"]:
                if state["reader_state"] == 3 or state["reader_transport_status"]:
                    raise ProtocolError(
                        "Reader transport error: " + str(state["reader_transport_status"])
                    )
                if state["reader_state"] == 2:
                    reply = state["reader_reply"]
                    if len(reply) < 6 or reply[0] != 0x94 or reply[1] != 0:
                        raise ProtocolError(
                            "Reader rejected or returned invalid power data: " + str(reply)
                        )
                    powers = dict(zip(["ANT1", "ANT2", "ANT3", "ANT4"], reply[2:6]))
                    break
            now = time.monotonic()
            if now >= deadline:
                raise ProtocolError("Reader reply timeout")
            if now >= next_heartbeat:
                accepted(device.heartbeat())
                next_heartbeat = time.monotonic() + 0.5
            time.sleep(0.05)
        accepted(device.command("maintenance", [0]))
        accepted(device.release())
        claimed = False
        print(json.dumps({"power_dbm": powers, "reader_sequence": ack["sequence"]}))
        completed = True
    finally:
        if claimed:
            try:
                state = device.status()
                if state["owner"] == 6 and state["owner_client"] == device.client_id:
                    if state["reader_state"] != 1:
                        accepted(device.command("maintenance", [0]))
                    accepted(device.release())
            except Exception as exc:
                print(
                    "Cleanup not confirmed; inspect live state. Lease expires without heartbeat: "
                    + str(exc),
                    file=sys.stderr,
                )
        device.close()
    return completed


if __name__ == "__main__":
    try:
        main()
    except (ProtocolError, OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
