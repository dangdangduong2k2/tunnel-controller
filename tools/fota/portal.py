"""Single-page, no-login Tunnel FOTA console. Run one process behind HTTPS."""

import argparse
import atexit
from contextlib import closing
import hashlib
import os
from pathlib import Path
import re
import sqlite3
import sys
import threading
import time
from urllib.parse import urlsplit

from flask import Flask, jsonify, request, send_file
from werkzeug.exceptions import HTTPException

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "api"))
from tunnel_sdk.fota import FotaClient, signing_key_id
from tunnel_sdk.firmware_image import inspect_firmware
from tunnel_sdk.errors import ProtocolError, UnknownOutcome
from mqtt_discovery import Discovery

LIMIT = 0x330000
TERMINAL = {"success", "failed", "rolled_back", "interrupted", "rejected"}


def validate_image(data):
    return inspect_firmware(data)


def create_app(data_dir=None, client_factory=FotaClient, config=None):
    app = Flask(__name__, static_folder=None)
    app.config.update(
        MAX_CONTENT_LENGTH=LIMIT + 65536,
        TRUSTED_HOSTS=["localhost", "127.0.0.1", "103-9-78-139.sslip.io"],
        BROKER_HOST=os.getenv("TUNNEL_MQTT_HOST", "broker.emqx.io"),
        BROKER_PORT=int(os.getenv("TUNNEL_MQTT_PORT", "1883")),
        BROKER_USER=os.getenv("TUNNEL_MQTT_USER", ""),
        BROKER_PASSWORD=os.getenv("TUNNEL_MQTT_PASSWORD", ""),
        DISCOVERY_START=True,
        PUBLIC_BASE=os.getenv("TUNNEL_FIRMWARE_BASE", ""),
        SIGNING_KEY=os.getenv("TUNNEL_SIGNING_KEY", ""),
        MONITOR_SECONDS=180,
        POLL_SECONDS=2,
    )
    app.config.update(config or {})
    folder = Path(data_dir or REPO / "private" / "fota-portal").resolve()
    folder.mkdir(parents=True, exist_ok=True)
    binaries = folder / "firmware"
    binaries.mkdir(exist_ok=True)
    dbfile = folder / "portal.sqlite3"

    def db():
        c = sqlite3.connect(dbfile, timeout=10)
        c.row_factory = sqlite3.Row
        return c

    with closing(db()) as c, c:
        c.executescript("""
            CREATE TABLE IF NOT EXISTS releases (
                sha TEXT PRIMARY KEY, name TEXT, version INTEGER, size INTEGER, created REAL);
            CREATE TABLE IF NOT EXISTS jobs (
                id TEXT PRIMARY KEY, sha TEXT, root TEXT, version INTEGER,
                state TEXT, percent INTEGER DEFAULT 0, detail TEXT DEFAULT '', created REAL,
                updated REAL, active INTEGER DEFAULT 1);
            CREATE UNIQUE INDEX IF NOT EXISTS one_active_job ON jobs(root) WHERE active=1;
        """)
        c.execute(
            "UPDATE jobs SET state='unknown', detail='Server restarted; check board status' WHERE active=1"
        )
        if "device_id" not in [r[1] for r in c.execute("PRAGMA table_info(jobs)")]:
            c.execute("ALTER TABLE jobs ADD COLUMN device_id TEXT DEFAULT ''")
        if "image_schema" not in [r[1] for r in c.execute("PRAGMA table_info(releases)")]:
            c.execute("ALTER TABLE releases ADD COLUMN image_schema INTEGER DEFAULT 0")

    discovery = Discovery(db, app.config)
    app.extensions["discovery"] = discovery
    if app.config["DISCOVERY_START"]:
        discovery.start()
        atexit.register(discovery.stop)

    def connection(root):
        return client_factory(
            app.config["BROKER_HOST"],
            port=app.config["BROKER_PORT"],
            username=app.config["BROKER_USER"],
            password=app.config["BROKER_PASSWORD"],
            root=root,
            timeout=8,
        )

    def setup_status():
        base = app.config["PUBLIC_BASE"]
        problems = []
        key_id = None
        try:
            parsed = urlsplit(base)
            valid = (
                parsed.scheme == "https"
                and parsed.hostname
                and not parsed.username
                and not parsed.query
                and not parsed.fragment
                and base.endswith("/")
                and len(base) <= 220
                and not any(c.isspace() or c in '\\"' for c in base)
            )
        except ValueError:
            valid = False
        if not valid:
            problems.append("Cấu hình HTTPS download prefix hợp lệ, kết thúc bằng /.")
        try:
            key_id = signing_key_id(app.config["SIGNING_KEY"])
        except Exception:
            problems.append("Cấu hình file khóa ký P-256 hợp lệ trên server.")
        return dict(ready=not problems, problems=problems, url_prefix=base, signing_key_id=key_id)

    def ready():
        return setup_status()["ready"]

    def update_job(job, state, percent=0, detail=""):
        with closing(db()) as c, c:
            c.execute(
                "UPDATE jobs SET state=?,percent=?,detail=?,updated=?,active=? WHERE id=?",
                (
                    state,
                    max(0, min(100, int(percent))),
                    str(detail)[:200],
                    time.time(),
                    0 if state in TERMINAL else 1,
                    job,
                ),
            )

    def apply_status(root, status):
        if not isinstance(status, dict) or status.get("state") not in TERMINAL | {
            "downloading",
            "rebooting",
            "verifying",
            "accepted",
            "verify_boot",
            "boot_verifying",
        }:
            return
        with closing(db()) as c:
            row = c.execute(
                "SELECT * FROM jobs WHERE id=? AND root=? AND active=1", (status.get("job"), root)
            ).fetchone()
        if not row:
            return
        if status.get("state") == "success" and status.get("running_version") != row["version"]:
            return
        percent = 100 if status["state"] == "success" else status.get("percent", 0)
        if (
            isinstance(percent, bool)
            or not isinstance(percent, (int, float))
            or not 0 <= percent <= 100
        ):
            return
        update_job(row["id"], status["state"], percent, status.get("error", ""))

    def run_job(job, release, root):
        sent = False
        try:
            with closing(db()) as c:
                device_id = c.execute("SELECT device_id FROM jobs WHERE id=?", (job,)).fetchone()[0]
            device = discovery.select(device_id, require_live=True)
            if device["root"] != root:
                raise ValueError("Device root changed before dispatch")
            if (
                hashlib.sha256((binaries / (release["sha"] + ".bin")).read_bytes()).hexdigest()
                != release["sha"]
            ):
                raise ValueError("Stored image changed")
            identity = validate_image((binaries / (release["sha"] + ".bin")).read_bytes())
            if identity["version"] != release["version"]:
                raise ValueError("Stored image version does not match its release")
            with connection(root) as client:
                sent = True
                ack = client.update_firmware(
                    url=app.config["PUBLIC_BASE"] + release["sha"] + ".bin",
                    firmware=binaries / (release["sha"] + ".bin"),
                    version=release["version"],
                    signing_key=app.config["SIGNING_KEY"],
                    job=job,
                )
                if ack.get("state") not in ("accepted", "duplicate"):
                    update_job(job, "rejected", detail=ack.get("error", "No acceptance"))
                    return
                update_job(job, "accepted")
                end = time.monotonic() + app.config["MONITOR_SECONDS"]
                while time.monotonic() < end:
                    try:
                        status = client.get_update_status()
                        apply_status(root, status)
                        if status.get("job") == job and status.get("state") in TERMINAL:
                            with closing(db()) as c:
                                if not c.execute(
                                    "SELECT active FROM jobs WHERE id=?", (job,)
                                ).fetchone()[0]:
                                    return
                    except Exception:
                        pass  # MQTT outage does not mean firmware failed; never resend.
                    time.sleep(app.config["POLL_SECONDS"])
                update_job(
                    job, "unknown", detail="No final status. Check board; command was not retried."
                )
        except UnknownOutcome:
            update_job(job, "unknown", detail="No ACK. Check board; command was not retried.")
        except (ValueError, ProtocolError) as exc:
            update_job(
                job,
                "rejected",
                detail=str(exc)[:200] or "Preflight refused",
            )
        except Exception:
            update_job(
                job,
                "unknown" if sent else "failed",
                detail="Connection or command failed. Check board status before another update.",
            )

    app.extensions["fota_run_job"] = run_job

    @app.before_request
    def protect_browser_writes():
        # Not authentication: prevents drive-by cross-origin form submissions.
        if request.method == "POST":
            if request.headers.get("X-Tunnel-Request") != "1":
                return jsonify(error="Request must originate from Tunnel console."), 403
            origin = request.headers.get("Origin")
            if origin and urlsplit(origin).netloc != request.host:
                return jsonify(error="Cross-origin request refused."), 403

    @app.after_request
    def headers(response):
        response.headers["Cache-Control"] = "no-store"
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["X-Frame-Options"] = "DENY"
        response.headers["Referrer-Policy"] = "no-referrer"
        response.headers["Content-Security-Policy"] = (
            "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"
        )
        return response

    @app.errorhandler(ValueError)
    def invalid(e):
        return jsonify(error=str(e)), 400

    @app.errorhandler(HTTPException)
    def http_error(e):
        return jsonify(error="File quá lớn." if e.code == 413 else e.description), e.code

    @app.get("/")
    @app.get("/tunnel/admin/")
    def index():
        return send_file(Path(__file__).with_name("portal.html"))

    @app.get("/tunnel/admin/app.js")
    def javascript():
        return send_file(Path(__file__).with_name("portal.js"), mimetype="text/javascript")

    @app.get("/tunnel/admin/api/overview")
    def overview():
        with closing(db()) as c:
            releases = [dict(r) for r in c.execute("SELECT * FROM releases ORDER BY created DESC")]
            jobs = [
                dict(r) for r in c.execute("SELECT * FROM jobs ORDER BY created DESC LIMIT 100")
            ]
        return jsonify(
            releases=releases,
            jobs=jobs,
            ready=ready(),
            setup=setup_status(),
            devices=discovery.devices(),
            discovery={"connected": discovery.connected, "state": discovery.state},
            broker=app.config["BROKER_HOST"] + ":" + str(app.config["BROKER_PORT"]),
        )

    @app.post("/tunnel/admin/api/upload")
    def upload():
        file = request.files.get("file")
        if not file or not (file.filename or "").lower().endswith(".bin"):
            raise ValueError("Chọn file firmware .bin.")
        data = file.read(LIMIT + 1)
        identity = validate_image(data)
        version = identity["version"]
        supplied = request.form.get("version")
        if supplied is not None and supplied != str(version):
            raise ValueError("Mã phiên bản nhập không khớp phiên bản nằm trong file firmware.")
        sha = hashlib.sha256(data).hexdigest()
        with closing(db()) as c, c:
            c.execute("BEGIN IMMEDIATE")
            existing = c.execute("SELECT * FROM releases WHERE sha=?", (sha,)).fetchone()
            if existing:
                if existing["version"] != int(version):
                    raise ValueError("File này đã có mã phiên bản khác.")
                return jsonify(dict(existing))
            total = c.execute("SELECT COALESCE(SUM(size),0) FROM releases").fetchone()[0]
            if total + len(data) > 100 * 1024 * 1024:
                raise ValueError("Kho firmware đã đầy (100 MiB).")
            path = binaries / (sha + ".bin")
            path.write_bytes(data)
            name = Path(file.filename.replace("\\", "/")).name[:120]
            c.execute(
                "INSERT INTO releases(sha,name,version,size,created,image_schema) VALUES(?,?,?,?,?,?)",
                (sha, name, int(version), len(data), time.time(), identity["schema"]),
            )
        return jsonify(sha=sha, version=version, target=identity["target"]), 201

    @app.get("/tunnel/firmware/<sha>.bin")
    def artifact(sha):
        if not re.fullmatch(r"[0-9a-f]{64}", sha):
            return jsonify(error="Not found"), 404
        with closing(db()) as c:
            if not c.execute("SELECT 1 FROM releases WHERE sha=?", (sha,)).fetchone():
                return jsonify(error="Not found"), 404
        return send_file(
            binaries / (sha + ".bin"), mimetype="application/octet-stream", conditional=False
        )

    @app.post("/tunnel/admin/api/status")
    def status():
        body = request.get_json(silent=True)
        device = discovery.select(body.get("device_id") if isinstance(body, dict) else None)
        try:
            with connection(device["root"]) as client:
                machine = client.status()
                try:
                    ota = client.get_update_status()
                    apply_status(device["root"], ota)
                except Exception:
                    ota = None
            return jsonify(online=True, machine=machine, ota=ota, checked=time.time())
        except Exception:
            return jsonify(
                online=False,
                error="Không nhận được phản hồi. Kiểm tra LAN, broker và topic root.",
                checked=time.time(),
            )

    @app.post("/tunnel/admin/api/deploy")
    def deploy():
        if not ready():
            return jsonify(error="Chưa cấu hình URL HTTPS và khóa ký trên server."), 409
        body = request.get_json(silent=True)
        if not isinstance(body, dict) or set(body) != {"sha", "request_id", "device_id"}:
            raise ValueError("Thiếu firmware hoặc mã yêu cầu.")
        if not isinstance(body["request_id"], str) or not re.fullmatch(
            r"[0-9a-f]{32}", body["request_id"]
        ):
            raise ValueError("Mã yêu cầu không hợp lệ.")
        if not isinstance(body["sha"], str):
            raise ValueError("Firmware không hợp lệ.")
        device = discovery.select(body["device_id"], require_live=True)
        if device["firmware"] < 1160:
            raise ValueError("Bo cần nạp 1.16.0 qua USB một lần để sử dụng OTA mới.")
        with closing(db()) as c, c:
            c.execute("BEGIN IMMEDIATE")
            old = c.execute("SELECT * FROM jobs WHERE id=?", (body["request_id"],)).fetchone()
            if old:
                if old["sha"] != body["sha"] or old["device_id"] != body["device_id"]:
                    return jsonify(error="Mã yêu cầu đã được dùng cho file khác."), 409
                return jsonify(dict(old))
            release = c.execute("SELECT * FROM releases WHERE sha=?", (body["sha"],)).fetchone()
            if not release:
                raise ValueError("Firmware chưa được tải lên.")
            if release["image_schema"] != 1:
                raise ValueError("File cũ chưa có nhận diện OTA. Tải app.bin từ 1.16.0 trở lên.")
            if c.execute(
                "SELECT 1 FROM jobs WHERE (root=? OR device_id=?) AND active=1",
                (device["root"], device["device_id"]),
            ).fetchone():
                return jsonify(
                    error="Bo có lượt cập nhật chưa kết thúc. Bấm Kiểm tra bo để đối chiếu."
                ), 409
            now = time.time()
            c.execute(
                "INSERT INTO jobs(id,sha,root,version,state,created,updated,device_id) VALUES(?,?,?,?,?,?,?,?)",
                (
                    body["request_id"],
                    release["sha"],
                    device["root"],
                    release["version"],
                    "sending",
                    now,
                    now,
                    device["device_id"],
                ),
            )
        threading.Thread(
            target=run_job, args=(body["request_id"], dict(release), device["root"]), daemon=True
        ).start()
        return jsonify(job=body["request_id"]), 202

    return app


if __name__ == "__main__":
    from waitress import serve

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8090)
    parser.add_argument("--data", type=Path)
    parser.add_argument(
        "--config", type=Path, help="Server settings JSON; secrets stay server-side"
    )
    args = parser.parse_args()
    import json

    config = json.loads(args.config.read_text(encoding="utf-8-sig")) if args.config else None
    print(f"Tunnel console: http://{args.bind}:{args.port}/tunnel/admin/", flush=True)
    serve(create_app(args.data, config=config), host=args.bind, port=args.port, threads=4)
