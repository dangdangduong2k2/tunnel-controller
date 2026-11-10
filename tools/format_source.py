"""Apply the project formatter; generated files and vendored code are excluded."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def executable(name):
    local = Path(sys.executable).parent / (name + ".exe" if sys.platform == "win32" else name)
    found = str(local) if local.exists() else shutil.which(name)
    if not found:
        raise SystemExit("Missing " + name + "; see docs/BUILD_TEST.md")
    return found


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--check", action="store_true")
    args = p.parse_args()
    formatter = executable("clang-format")
    version = subprocess.check_output([formatter, "--version"], text=True)
    if "18.1.8" not in version:
        raise SystemExit("Use clang-format 18.1.8")
    files = []
    for folder in ("firmware", "tests"):
        for f in (ROOT / folder).rglob("*"):
            if f.suffix not in (".c", ".cpp", ".h", ".ino") or "vendor" in f.parts:
                continue
            if f.read_text(encoding="utf-8").startswith("// Generated"):
                continue
            files.append(str(f))
    flags = ["--dry-run", "--Werror"] if args.check else ["-i"]
    for start in range(0, len(files), 30):
        subprocess.run([formatter, *flags, *files[start : start + 30]], check=True, cwd=ROOT)
    ruff = executable("ruff")
    args2 = ["--check"] if args.check else []
    subprocess.run([ruff, "format", *args2, "api", "tools", "tests"], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
