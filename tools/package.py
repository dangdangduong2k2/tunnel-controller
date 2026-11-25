"""Package a clean source release plus the verified app; exclude caches and device secrets."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DIRS = ("api", "contracts", "docs", "firmware", "libraries", "tests", "tools", "web", "validation")
FILES = (
    "README.md",
    "CHANGELOG.md",
    "BUILD.ps1",
    "RUN_TESTS.cmd",
    ".editorconfig",
    ".clang-format",
    ".gitignore",
    "pyproject.toml",
    "source_layout.json",
)


def main():
    subprocess.run([sys.executable, str(ROOT / "tools/check_project.py")], check=True)
    release = json.loads((ROOT / "contracts/release.json").read_text(encoding="utf-8"))
    build = json.loads((ROOT / "dist/build.json").read_text(encoding="utf-8"))
    evidence = json.loads((ROOT / "validation/results.json").read_text(encoding="utf-8"))
    if build["release"] != release or evidence.get("status") != "passed":
        raise SystemExit("Missing matching successful build/validation")
    binary = ROOT / "dist" / build["app"]
    if hashlib.sha256(binary.read_bytes()).hexdigest() != build["app_sha256"]:
        raise SystemExit("App hash mismatch")
    files = [ROOT / name for name in FILES]
    for directory in DIRS:
        files += [
            p
            for p in (ROOT / directory).rglob("*")
            if p.is_file()
            and not {"__pycache__", ".venv", "private", ".ruff_cache"}.intersection(
                p.relative_to(ROOT).parts
            )
            and p.suffix not in (".pyc", ".pyo")
            and p.name not in ("mqtt_config.json", "settings.json", "FotaTrust.local.h")
        ]
    files += [binary, ROOT / "dist/build.json", ROOT / "dist/SHA256SUMS.txt"]
    manifest = {
        str(p.relative_to(ROOT)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(files)
    }
    evidence_hashes = evidence["tested_sources"]
    for name, digest in evidence_hashes.items():
        path = ROOT / name
        if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise SystemExit("Source changed since validation: " + name)
    archive = ROOT.parent / (
        "Tunnel_Controller_v" + release["version"].replace(".", "_") + "_Source.zip"
    )
    prefix = ROOT.name + "/"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(files):
            z.write(p, prefix + p.relative_to(ROOT).as_posix())
        z.writestr(prefix + "SOURCE_MANIFEST.json", json.dumps(manifest, indent=2) + "\n")
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise SystemExit("ZIP verification failed")
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".sha256").write_text(digest + "  " + archive.name + "\n", encoding="utf-8")
    print(
        json.dumps(
            dict(
                archive=str(archive), files=len(files), bytes=archive.stat().st_size, sha256=digest
            ),
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
