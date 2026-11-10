"""Read-only diagnostic: one FC04 request, no claim/heartbeat or machine command."""

import argparse
import json
from pathlib import Path
import socket
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "api"))
from tunnel_sdk import crc16 as crc16  # Re-export for diagnostic clients/tests.


def exact(stream, count):
    result = b""
    while len(result) < count:
        chunk = stream.recv(count - len(result))
        if not chunk:
            raise RuntimeError("Incomplete response")
        result += chunk
    return result


def decode(pdu):
    if len(pdu) == 2 and pdu[0] & 128:
        raise RuntimeError("Modbus exception " + str(pdu[1]))
    if len(pdu) != 66 or pdu[:2] != bytes([4, 64]):
        raise RuntimeError("Expected FC04 and 32 registers")
    r = list(struct.unpack(">32H", pdu[2:]))
    return dict(
        registers=r,
        firmware=r[1],
        state=r[2],
        fault=r[3],
        mode=r[4],
        unique_epcs=r[10],
        total_reads=(r[11] << 16) | r[12],
        owner=r[16],
        lease_ms=r[17],
        ip=".".join(map(str, (r[25] >> 8, r[25] & 255, r[26] >> 8, r[26] & 255))),
    )


def read_tcp(host, port=502, unit_id=1):
    if not 1 <= unit_id <= 247:
        raise ValueError("Unit ID must be 1..247")
    with socket.create_connection((host, port), timeout=2) as stream:
        stream.sendall(struct.pack(">HHHBBHH", 1, 0, 6, unit_id, 4, 0, 32))
        tid, protocol, length, unit = struct.unpack(">HHHB", exact(stream, 7))
        if (tid, protocol, unit) != (1, 0, unit_id) or not 2 <= length <= 254:
            raise RuntimeError("Invalid MBAP")
        return decode(exact(stream, length - 1))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True)
    p.add_argument("--port", type=int, default=502)
    p.add_argument("--unit", type=int, default=1)
    args = p.parse_args()
    print(json.dumps(read_tcp(args.host, args.port, args.unit), indent=2))
