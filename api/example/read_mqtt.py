"""Read-only MQTT /get example; credentials from environment."""

import argparse
import json
import os
from tunnel_sdk import TunnelDevice


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True)
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--root", default="tunnel/01/v1")
    a = p.parse_args()
    with TunnelDevice.mqtt(
        a.host,
        port=a.port,
        root=a.root,
        username=os.getenv("TUNNEL_MQTT_USERNAME", ""),
        password=os.getenv("TUNNEL_MQTT_PASSWORD", ""),
    ) as device:
        print(json.dumps(device.get_status(), indent=2))


if __name__ == "__main__":
    main()
