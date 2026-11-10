"""Read EPC counts without homing or motor commands; firmware 1.12.1+ required."""

import json
import sys
import time

from tunnel_sdk import ProtocolError
from device_console import Console, make_device, parser


def scan_tags(device, seconds=8, emit=print):
    if not 0 < seconds <= 25:
        raise ValueError("seconds must be >0 and <=25 (firmware has a 30-second limit)")
    state = device.get_status()
    if state["firmware"] < 1121:
        raise ProtocolError("Independent scan requires firmware 1.12.1+; do not auto-Reset")
    if state["owner"] or state["activity_flags"] & 31 or state["reader_state"] == 1:
        raise ProtocolError("Machine must be idle, unowned, outside maintenance")
    console = Console(device)
    last = state
    try:
        console.run("claim")
        console.run("scan", {"run": 1})
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            last = console.run("status")
            with console.lock:
                console._check()
            if last["activity_flags"] & 7:
                raise ProtocolError("Unexpected motor activity; stop and inspect")
            if last["scan_result"] >= 3:
                raise ProtocolError(f"Reader scan error {last['scan_result']}")
            emit(
                json.dumps(
                    {
                        k: last[k]
                        for k in ("epc_count", "total_reads", "scan_elapsed_ms", "scan_result")
                    }
                )
            )
            time.sleep(0.25)
        console.run("scan", {"run": 0})
        deadline = time.monotonic() + 7
        while time.monotonic() < deadline:
            last = console.run("status")
            if not last["activity_flags"] & 8 and not last["reader_flags"] & 4:
                return {k: last[k] for k in ("epc_count", "total_reads", "scan_result")}
            time.sleep(0.1)
        raise ProtocolError("Reader STOP not confirmed; inspect live state")
    finally:
        console.close()  # Release/STOP even when scan or its ACK fails; never Reset.


def main():
    p = parser()
    p.description = __doc__
    p.add_argument("--seconds", type=float, default=8)
    a = p.parse_args()
    if a.command:
        p.error("Use device_console.py for --command; this script only scans tags")
    if not 0 < a.seconds <= 25:
        p.error("--seconds must be >0 and <=25")
    with make_device(p, a) as device:
        result = scan_tags(device, a.seconds)
        print(json.dumps({"final": result}))


if __name__ == "__main__":
    try:
        main()
    except (ProtocolError, KeyboardInterrupt) as exc:
        print(f"Scan stopped: {exc}", file=sys.stderr)
        sys.exit(1)
