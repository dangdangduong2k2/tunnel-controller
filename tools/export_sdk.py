"""Export a standalone Python SDK tree; never overwrite an existing destination."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination.exists():
        parser.error("Destination already exists; choose a new directory")
    destination.mkdir(parents=True)
    files = ["README.md", "LICENSE", "CHANGELOG.md", "pyproject.toml", "MANIFEST.in",
             "requirements.txt", ".gitignore", "tunnel_api.py",
             "settings.tcp.example.json", "settings.rtu.example.json", "mqtt_config.example.json"]
    for name in files:
        shutil.copy2(ROOT / "api" / name, destination / name)
    for name in ("tunnel_sdk", "docs", "example", "tests", ".github"):
        shutil.copytree(ROOT / "api" / name, destination / name,
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    print(destination)


if __name__ == "__main__":
    main()
