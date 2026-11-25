"""Run the bench portal and HTTPS downloads using already-provisioned test keys."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import threading

from https_server import server
from portal import REPO, create_app
from tunnel_sdk.fota import signing_key_id
from waitress import serve


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--host", required=True, help="This PC's LAN IP already provisioned in board trust"
    )
    p.add_argument("--port", type=int, default=8092, help="Loopback-only admin HTTP port")
    p.add_argument("--keys", type=Path, default=REPO / "private/fota-test")
    p.add_argument("--data", type=Path, default=REPO / "private/fota-portal")
    p.add_argument("--config", type=Path, help="Optional JSON broker settings")
    a = p.parse_args()
    prefix = f"https://{a.host}:8443/firmware/"
    header = (REPO / "firmware/TunnelESP32/src/update/FotaTrust.local.h").read_text()
    match = re.search(r'#define\s+TUNNEL_FOTA_URL_PREFIX\s+("[^"\n]+")', header)
    if not match or json.loads(match[1]) != prefix:
        raise SystemExit(
            "Local firmware trust URL differs; provision/rebuild before this bench test."
        )
    public = re.search(r"-----BEGIN PUBLIC KEY-----.*?-----END PUBLIC KEY-----\n", header, re.S)
    if not public or hashlib.sha256(public[0].encode()).hexdigest() != signing_key_id(
        a.keys / "release.key"
    ):
        raise SystemExit("Local release key differs from the firmware public key.")
    config = json.loads(a.config.read_text(encoding="utf-8-sig")) if a.config else {}
    config.update(PUBLIC_BASE=prefix, SIGNING_KEY=str((a.keys / "release.key").resolve()))
    app = create_app(a.data, config=config)
    https = server(a.data / "firmware", a.keys / "server.pem", a.keys / "server.key", a.host, 8443)
    threading.Thread(target=https.serve_forever, daemon=True).start()
    print(f"Admin: http://127.0.0.1:{a.port}/tunnel/admin/", flush=True)
    print(f"Firmware downloads: {prefix} (local bench only)", flush=True)
    try:
        serve(app, host="127.0.0.1", port=a.port, threads=4)
    finally:
        app.extensions["discovery"].stop()
        https.shutdown()
        https.server_close()


if __name__ == "__main__":
    main()
