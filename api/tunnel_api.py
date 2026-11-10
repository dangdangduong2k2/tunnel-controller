"""Compatibility import/CLI. New integrations may import tunnel_sdk directly."""

import json
from tunnel_sdk import (
    Modbus,
    Mqtt,
    ProtocolError,
    UnknownOutcome,
    decode_status,
    crc16,
    words,
    OPERATIONS,
    from_settings,
)

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Read-only status using settings.json")
    parser.add_argument("--settings", default="settings.json")
    options = parser.parse_args()
    with open(options.settings, encoding="utf-8-sig") as f:
        configuration = json.load(f)
    connection = from_settings(configuration)
    try:
        print(json.dumps(connection.status(), indent=2))
    finally:
        connection.close()

__all__ = [
    "Modbus",
    "Mqtt",
    "ProtocolError",
    "UnknownOutcome",
    "decode_status",
    "crc16",
    "words",
    "OPERATIONS",
    "from_settings",
]
