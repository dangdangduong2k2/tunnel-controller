"""Browser integration with isolated HTTP/SQLite and a fake board, never MQTT."""

import argparse
import json
from pathlib import Path
import threading
import time

from playwright.sync_api import sync_playwright, expect
from werkzeug.serving import make_server
import test_fota_portal as fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screenshots", type=Path)
    parser.add_argument(
        "--channel", help="Installed browser, e.g. msedge; default Playwright Chromium"
    )
    args = parser.parse_args()
    fixture = fixtures.PortalTest()
    fixture.setUp()
    server = make_server("127.0.0.1", 0, fixture.app)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f"http://127.0.0.1:{server.server_port}/tunnel/admin/"
    errors = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(channel=args.channel)
            page = browser.new_page(viewport={"width": 1280, "height": 960})
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(url)
            expect(page.locator("#device-name")).to_have_text("Chưa có thiết bị")
            expect(page.locator("#deploy")).to_be_disabled()
            page.locator("#file").set_input_files(
                {"name": "wrong.bin", "mimeType": "application/octet-stream", "buffer": b"invalid"}
            )
            page.locator("#upload-btn").click()
            expect(page.locator("#message")).to_contain_text("application .bin")
            page.locator("#file").set_input_files(
                {
                    "name": "Tunnel_TEST_1161.bin",
                    "mimeType": "application/octet-stream",
                    "buffer": fixtures.image(),
                }
            )
            page.locator("#upload-btn").click()
            expect(page.locator("#release option")).to_contain_text("1161")
            expect(page.locator("#deploy")).to_be_disabled()
            if args.screenshots:
                args.screenshots.mkdir(parents=True, exist_ok=True)
                page.screenshot(path=str(args.screenshots / "desktop-empty.png"), full_page=True)
            for running, action in [(1160, "Cập nhật"), (1161, "Nạp lại"), (1162, "Hạ phiên bản")]:
                fixture.announce(firmware=running)
                page.reload()
                expect(page.locator("#deploy")).to_be_enabled()
                page.locator("#deploy").click()
                expect(page.locator("#confirm")).to_be_visible()
                expect(page.locator("#confirm-text")).to_contain_text(action)
                expect(page.locator("#confirm-text")).to_contain_text(f"{running} → 1161")
                page.locator("#cancel").click()
                expect(page.locator("#confirm")).not_to_be_visible()
                assert fixture.overview()["jobs"] == [], "Cancel must not create a job"
            page.locator("#deploy").click()
            page.locator("#send").click()
            expect(page.locator("#confirm")).not_to_be_visible()
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if fixture.overview()["jobs"][0]["state"] == "success":
                    break
                time.sleep(0.05)
            assert fixture.overview()["jobs"][0]["state"] == "success"
            page.reload()
            expect(page.locator("#history progress")).to_have_attribute("value", "100")
            page.set_viewport_size({"width": 390, "height": 844})
            assert page.evaluate(
                "document.documentElement.scrollWidth <= innerWidth"
            ), "Mobile overflow"
            if args.screenshots:
                page.screenshot(
                    path=str(args.screenshots / "mobile-simulated-success.png"), full_page=True
                )
            assert not errors, errors
            browser.close()
        print(
            json.dumps(
                {
                    "browser": "PASS",
                    "real_mqtt_commands": 0,
                    "checks": [
                        "empty registry",
                        "invalid upload",
                        "auto version",
                        "cancel",
                        "upgrade/reinstall/downgrade confirmation",
                        "simulated update",
                        "progress",
                        "mobile fit",
                        "no JS errors",
                    ],
                }
            )
        )
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
        fixture.doCleanups()


if __name__ == "__main__":
    main()
