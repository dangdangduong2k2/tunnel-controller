"""Explicitly query or submit a signed FOTA job; never retries a command."""

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "api"))
from tunnel_sdk.fota import FotaClient

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=["status", "update"])
    p.add_argument("--host", required=True)
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--root", required=True)
    p.add_argument("--url")
    p.add_argument("--firmware", type=Path)
    p.add_argument("--version", type=int)
    p.add_argument("--signing-key", type=Path)
    a = p.parse_args()
    if a.action == "update" and not all((a.url, a.firmware, a.version, a.signing_key)):
        p.error("update requires --url --firmware --version --signing-key")
    with FotaClient(a.host, port=a.port, root=a.root, timeout=8) as c:
        result = (
            c.get_update_status()
            if a.action == "status"
            else c.update_firmware(
                url=a.url, firmware=a.firmware, version=a.version, signing_key=a.signing_key
            )
        )
        print(json.dumps(result, indent=2))
