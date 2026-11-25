"""Check generated files, documentation links, component coverage and Python syntax."""

import ast
import json
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
# Machine-local credential notes are gitignored and are not project documentation.
EXCLUDED = {"build", "test_build", "dist", ".venv", "__pycache__", "private"}


def main():
    subprocess.run([sys.executable, str(ROOT / "tools/generate.py"), "--check"], check=True)
    errors = []
    docs = 0
    for path in ROOT.rglob("*"):
        if not path.is_file() or EXCLUDED.intersection(path.relative_to(ROOT).parts):
            continue
        if path.suffix == ".py":
            try:
                ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
            except SyntaxError as exc:
                errors.append(str(exc))
        if path.suffix != ".md":
            continue
        docs += 1
        text = path.read_text(encoding="utf-8")
        for target in re.findall(r"\[[^\]]*\]\(([^\n)]+)\)", text):
            if target.startswith(("https://", "http://", "#", "mailto:")):
                continue
            target = unquote(target.split("#", 1)[0].strip("<>"))
            dest = (path.parent / target).resolve()
            if not dest.is_relative_to(ROOT.resolve()) or not dest.exists():
                errors.append(f"{path.relative_to(ROOT)}: missing/outside link {target}")
    for folder in (ROOT / "firmware/TunnelESP32/src").rglob("*"):
        if folder.is_dir() and not (folder / "README.md").is_file():
            errors.append("Missing component README: " + str(folder.relative_to(ROOT)))
    for name in (
        "README.md",
        "docs/VALIDATION.md",
        "tools/README.md",
        "api/README.md",
        "tests/README.md",
    ):
        if not (ROOT / name).exists():
            errors.append("Missing " + name)
    html = (ROOT / "web/index.html").read_text(encoding="utf-8")
    if 'TUNNELCFG"' in html or 'TUNNELWEB"' in html or "</html>" not in html:
        errors.append("Invalid extracted HTML/raw-string delimiter leaked into source")
    layout = json.loads((ROOT / "source_layout.json").read_text(encoding="utf-8"))
    for old, new in layout.items():
        if not (ROOT / new).is_file():
            errors.append("Lost migrated file " + old)
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"PASS project structure, Python syntax, {docs} Markdown files and local links")


if __name__ == "__main__":
    main()
