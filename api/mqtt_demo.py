"""Read-only MQTT monitoring: /get requests only, never /cmd or claim."""

import argparse
import json
import time
from tunnel_api import Mqtt


def main():
    parser = argparse.ArgumentParser(
        description="Monitor Tunnel MQTT without controlling the machine"
    )
    parser.add_argument("--config", default="mqtt_config.json")
    parser.add_argument("--samples", type=int, default=0, help="0 = continuously")
    options = parser.parse_args()
    with open(options.config, encoding="utf-8-sig") as handle:
        config = json.load(handle)
    if not config.get("mqtt_host") or config["mqtt_host"] == "CHANGE_ME_BROKER":
        parser.error("Fill mqtt_host in mqtt_config.json first")
    if options.samples < 0:
        parser.error("samples must be >= 0")
    device = Mqtt(
        config["mqtt_host"],
        config.get("mqtt_port", 1883),
        username=config.get("mqtt_username", ""),
        password=config.get("mqtt_password", ""),
        root=config.get("mqtt_root", "tunnel/01/v1"),
    )
    try:
        n = 0
        while options.samples == 0 or n < options.samples:
            state = device.status()
            print(
                json.dumps(
                    {
                        k: state[k]
                        for k in (
                            "firmware",
                            "state",
                            "fault",
                            "mode",
                            "unique_epcs",
                            "total_reads",
                            "owner",
                            "lease_ms",
                        )
                    }
                ),
                flush=True,
            )
            n += 1
            if options.samples == 0 or n < options.samples:
                time.sleep(0.5)
    finally:
        device.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
