"""Build pinned ESP32 firmware; never opens a serial port or uploads."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "api"))
from tunnel_sdk.firmware_image import inspect_firmware


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cli", default=os.environ.get("ARDUINO_CLI"))
    args = p.parse_args()
    release = json.loads((ROOT / "contracts/release.json").read_text(encoding="utf-8"))
    candidates = [
        args.cli,
        shutil.which("arduino-cli"),
        str(
            Path(os.environ.get("ProgramFiles", "C:/Program Files"))
            / "Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
        ),
    ]
    cli = next((c for c in candidates if c and Path(c).is_file()), None)
    if not cli:
        raise SystemExit("Set ARDUINO_CLI or pass --cli path/to/arduino-cli")
    result = subprocess.check_output([cli, "core", "list", "--format", "json"], text=True)
    cores = json.loads(result)
    installed = cores if isinstance(cores, list) else cores.get("platforms", [])
    if not any(
        c.get("id") == "esp32:esp32"
        and (c.get("installed_version") or c.get("installed")) == release["arduino_core"]
        for c in installed
    ):
        raise SystemExit(
            "Install esp32:esp32@" + release["arduino_core"] + "; see docs/BUILD_TEST.md"
        )
    subprocess.run([sys.executable, str(ROOT / "tools/generate.py"), "--check"], check=True)
    subprocess.run(
        [
            cli,
            "compile",
            "--fqbn",
            release["fqbn"],
            "--libraries",
            str(ROOT / "libraries"),
            "--build-path",
            str(ROOT / "build"),
            str(ROOT / "firmware/TunnelESP32"),
        ],
        check=True,
    )
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    name = "TunnelESP32_v" + release["version"].replace(".", "_") + "_app.bin"
    target = dist / name
    source = ROOT / "build/TunnelESP32.ino.bin"
    identity = inspect_firmware(source.read_bytes())
    if identity["version"] != release["firmware_register"]:
        raise SystemExit("Built image identity disagrees with contracts/release.json")
    shutil.copyfile(source, target)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    (dist / "SHA256SUMS.txt").write_text(digest + "  " + name + "\n", encoding="utf-8")
    target.with_suffix(".release.json").write_text(
        json.dumps(dict(identity, file=name, version_label=release["version"]), indent=2) + "\n",
        encoding="utf-8",
    )
    (dist / "build.json").write_text(
        json.dumps(
            dict(
                release=release,
                app=name,
                app_sha256=digest,
                app_bytes=target.stat().st_size,
                flashed=False,
            ),
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print("Built", target, "SHA256", digest)


if __name__ == "__main__":
    main()
