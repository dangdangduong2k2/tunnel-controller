"""Headless Chrome + real production C++ config handlers, simulated NVS/GPIO.
Build config_service with RUN_TESTS.cmd first. pip install playwright==1.58.0.
Only loopback HTTP is opened. No physical board or COM port is accessed.
"""

import json
import pathlib
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from playwright.sync_api import sync_playwright, expect

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Device:
    def __init__(self):
        self.process = subprocess.Popen(
            [str(ROOT / "test_build/config_service/config_service.exe"), "--serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        self.lock = threading.Lock()
        self.last = time.monotonic()

    def call(self, data):
        with self.lock:
            now = time.monotonic()
            data["advance"] = int((now - self.last) * 1000)
            self.last = now
            self.process.stdin.write(json.dumps(data) + "\n")
            self.process.stdin.flush()
            raw = self.process.stdout.readline()
            if not raw:
                raise RuntimeError("Native service exited")
            return json.loads(raw)

    def close(self):
        self.process.stdin.close()
        self.process.wait(5)


def main():
    native = Device()
    count = 0

    def check(value, name):
        nonlocal count
        assert value, name
        count += 1
        print("PASS", name, flush=True)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def handle_request(self):
            data = {"path": self.path, "authorization": self.headers.get("Authorization", "")}
            if self.command == "POST" and int(self.headers.get("Content-Length", "0")):
                data["body"] = self.rfile.read(int(self.headers["Content-Length"])).decode()
            result = native.call(data)
            body = result["body"].encode()
            self.send_response(result["status"])
            content_type = "text/html" if self.path == "/" else "application/json"
            self.send_header("Content-Type", content_type + "; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        do_GET = handle_request
        do_POST = handle_request

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    captures = ROOT / "validation"
    captures.mkdir(exist_ok=True)
    errors = []
    try:
        check(
            native.call({"path": "/"})["body"]
            == (ROOT / "web/index.html").read_text(encoding="utf-8"),
            "compiled embedded page matches web source exactly",
        )
        with sync_playwright() as pw:
            browser = pw.chromium.launch(channel="chrome", headless=True)
            page = browser.new_page(viewport={"width": 1280, "height": 1000})
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(f"http://127.0.0.1:{server.server_address[1]}")
            check(
                page.title() == "Tunnel · Cấu hình thiết bị",
                "offline page loads without external assets",
            )
            page.locator("#loginPassword").fill("wrong")
            page.locator("#loginButton").click()
            expect(page.locator("#loginError")).to_contain_text("không đúng")
            check(True, "incorrect admin password rejected")
            page.locator("#loginPassword").fill("admin")
            page.locator("#loginButton").click()
            expect(page.locator("#settingsPanel")).to_be_visible()
            expect(page.locator("#saveButton")).to_be_enabled()
            check(
                page.locator("#admin_password").get_attribute("required") is not None,
                "first setup requires replacing admin password",
            )
            page.locator("#admin_password").fill("new-password-123")
            page.locator("#admin_confirm").fill("new-password-123")
            page.locator("#dhcp").uncheck()
            page.locator("#ip").fill("999.1.1.1")
            page.locator("#saveButton").click()
            expect(page.locator("#notice")).to_contain_text("IP")
            check(True, "invalid IPv4 rejected before persistence")
            page.locator("#dhcp").check()
            page.locator("#mqtt_enabled").check()
            page.locator("#mqtt_host").fill("broker.example.com")
            page.locator("#mqtt_user").fill("tunnel-app")
            page.locator("#mqtt_password").fill("broker-secret-123")
            page.locator("#unit").fill("7")
            page.locator("#tcp_port").fill("1502")
            page.locator("#baud").select_option("115200")
            page.locator("#parity").select_option("0")
            page.locator("#stop_bits").select_option("2")
            native.call({"busy": True})
            expect(page.locator("#saveButton")).to_be_disabled()
            check(
                page.locator("#mqtt_host").input_value() == "broker.example.com",
                "busy machine disables save without discarding draft",
            )
            native.call({"busy": False})
            expect(page.locator("#saveButton")).to_be_enabled()
            page.screenshot(path=str(captures / "config-desktop.png"), full_page=True)
            for width in (375, 768, 1024, 1440):
                page.set_viewport_size({"width": width, "height": 900})
                check(
                    page.evaluate("document.documentElement.scrollWidth<=window.innerWidth"),
                    f"no horizontal overflow at {width}px",
                )
                if width == 375:
                    page.screenshot(path=str(captures / "config-mobile.png"), full_page=True)
            page.set_viewport_size({"width": 1280, "height": 1000})
            page.locator("#saveButton").click()
            expect(page.locator("#reconnect")).to_be_visible(timeout=5000)
            check(True, "save acknowledges verified persistent write then restart")
            native.call({"cycle": True})
            page.reload()
            page.locator("#loginPassword").fill("admin")
            page.locator("#loginButton").click()
            expect(page.locator("#loginError")).to_contain_text("không đúng")
            page.locator("#loginPassword").fill("new-password-123")
            page.locator("#loginButton").click()
            expect(page.locator("#settingsPanel")).to_be_visible()
            check(
                page.locator("#mqtt_host").input_value() == "broker.example.com"
                and page.locator("#baud").input_value() == "115200"
                and page.locator("#unit").input_value() == "7",
                "broker, serial format and Unit ID survive simulated reboot",
            )
            check(
                page.locator("#mqtt_password").input_value() == ""
                and "Đã có" in page.locator("#secretHelp").inner_text(),
                "stored broker password redacted on reload",
            )
            page.locator("#mqtt_host").fill("replacement.example.com")
            page.locator("#saveButton").click()
            expect(page.locator("#reconnect")).to_be_visible()
            native.call({"cycle": True})
            page.reload()
            page.locator("#loginPassword").fill("new-password-123")
            page.locator("#loginButton").click()
            expect(page.locator("#settingsPanel")).to_be_visible()
            check(
                page.locator("#mqtt_host").input_value() == "replacement.example.com"
                and "Đã có" in page.locator("#secretHelp").inner_text(),
                "changing broker needs no firmware build and preserves omitted password",
            )
            page.locator("#logout").click()
            expect(page.locator("#loginPanel")).to_be_visible()
            check(not errors, "no browser JavaScript errors")
            browser.close()
        print(
            f"PASS {count} browser assertions; real configuration handlers, simulated NVS/GPIO",
            flush=True,
        )
    finally:
        server.shutdown()
        server.server_close()
        thread.join(3)
        native.close()


if __name__ == "__main__":
    main()
