"""Read-only RTU example. Use the USB-RS485 adapter COM port."""

import argparse
import json
from tunnel_sdk import TunnelDevice


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True)
    p.add_argument("--baud", type=int, default=19200)
    p.add_argument("--parity", choices=["N", "E", "O"], default="E")
    p.add_argument("--stop-bits", type=int, choices=[1, 2], default=1)
    p.add_argument("--unit", type=int, default=1)
    a = p.parse_args()
    with TunnelDevice.rtu(
        a.port, baud=a.baud, parity=a.parity, stop_bits=a.stop_bits, unit=a.unit
    ) as device:
        print(json.dumps(device.get_status(), indent=2))


if __name__ == "__main__":
    main()
