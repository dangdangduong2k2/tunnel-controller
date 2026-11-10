"""Read-only running-device check: capture HMI TX and request PC status (0x01).
Never sends Reset, motion, settings, or synthetic DWIN replies.
"""

from pathlib import Path
import argparse
import collections
import json
import re
import serial
import struct
import time


def crc(data):
    result = 0xFFFF
    for b in data:
        result ^= b
        for _ in range(8):
            result = (result >> 1) ^ (0x8408 if result & 1 else 0)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / "contracts/hmi_fields.json").read_text(encoding="utf8"))
    fields = {v[0]: (key, v[1]) for key, v in manifest["fields"].items()}
    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.timeout = 0.05
    port.dtr = False
    port.rts = False
    port.open()
    data = bytearray()
    start = time.monotonic()
    next_request = start + 2
    sequence = 600
    serial_error = None
    try:
        while time.monotonic() - start < 15:
            if time.monotonic() >= next_request:
                sequence += 1
                packet = bytes([0xA5, 0x5A, 3]) + struct.pack("<H", sequence) + b"\x01"
                packet += struct.pack("<H", crc(packet[2:]))
                port.write(b"@TUN:" + packet.hex().upper().encode() + b"\n")
                next_request += 2
            data += port.read(2048)
    except serial.SerialException as error:
        serial_error = str(error)
    finally:
        port.close()
    values = {}
    counts = collections.Counter()
    bad = []
    probes = 0
    frames = 0
    i = 0
    while i + 3 <= len(data):
        if data[i : i + 2] != b"\x5a\xa5":
            i += 1
            continue
        end = i + 3 + data[i + 2]
        if end > len(data):
            break
        f = data[i:end]
        frames += 1
        i = end
        if len(f) < 6:
            continue
        vp = int.from_bytes(f[4:6], "big")
        if f[3] == 0x83 and vp == 0x80:
            probes += 1
        if f[3] != 0x82 or vp not in fields:
            continue
        key, size = fields[vp]
        raw = f[6 : 6 + size]
        if len(raw) != size or b"\xff\xff" not in raw:
            bad.append(key)
            continue
        text, padding = raw.split(b"\xff\xff", 1)
        if any(padding):
            bad.append(key)
        values[key] = text.decode("ascii", errors="replace")
        counts[key] += 1
    statuses = []
    for raw in re.findall(rb"@TUN:([0-9A-F]+)\n", data):
        b = bytes.fromhex(raw.decode())
        if len(b) != 49 or b[:2] != b"\xa5\x5a" or b[5:7] != b"\x81\x00":
            continue
        if crc(b[2:-2]) != int.from_bytes(b[-2:], "little"):
            continue
        d = b[7:-2]
        flags = int.from_bytes(d[4:6], "little")
        statuses.append(
            dict(
                sequence=int.from_bytes(b[3:5], "little"),
                state=d[1],
                fault=d[2],
                mode=d[3],
                flags=flags,
                motion_active=bool(flags & (2 | 4 | 512 | 1024)),
                stop_reason=d[24],
                stop_flags=d[25],
                reset_reason=d[26],
                uptime_ms=int.from_bytes(d[28:32], "little"),
                uart_errors=int.from_bytes(d[32:36], "little"),
            )
        )
    result = dict(
        port=args.port,
        serial_error=serial_error,
        bytes_received=len(data),
        dgus_frames=frames,
        values=values,
        refresh_counts=dict(counts),
        invalid_fields=bad,
        config_read_probes=probes,
        statuses=statuses,
        all_fields_verified=len(values) == len(fields) and not bad,
        limitation="Observes ESP32 TX, not DWIN display/readback. Repeated config probes mean the firmware has not recognized a DWIN config response.",
    )
    (root / "hardware_uart_verification.json").write_text(
        json.dumps(result, indent=2), encoding="utf8"
    )
    print(json.dumps(result, indent=2))
    if (
        not result["all_fields_verified"]
        or len(statuses) < 2
        or any(s["motion_active"] or s["uart_errors"] for s in statuses)
    ):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
