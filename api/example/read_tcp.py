"""Read-only TCP example. No claim or motion."""

import argparse
import json
from tunnel_sdk import TunnelDevice


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True)
    p.add_argument("--port", type=int, default=502)
    p.add_argument("--unit", type=int, default=1)
    a = p.parse_args()
    with TunnelDevice.tcp(a.host, port=a.port, unit=a.unit) as device:
        print(json.dumps(device.get_status(), indent=2))


if __name__ == "__main__":
    main()
