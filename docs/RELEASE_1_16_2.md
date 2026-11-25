# 1.16.2 — public HTTPS OTA

## Change

The bench build switches from the private PC URL to
`https://103-9-78-139.sslip.io/tunnel/firmware/`. The existing P-256 release key
is retained, and the HTTPS trust anchor is ISRG Root X1. The public-only local
trust header is intentionally outside Git; private keys never enter firmware.
There are no changes to machine control, register map or version ordering.
An older build whose trust still points to the PC needs a one-time USB flash.

## VPS deployment, 2026-11-16

- Portal: `https://103-9-78-139.sslip.io/tunnel/admin/`.
- Dedicated `tunnel-ota.service`, loopback port 8090, no login as requested.
- Persistent files: `/var/lib/tunnel-ota`; configuration/key: `/etc/tunnel-ota`.
- Nginx configuration backed up as
  `/etc/nginx/sites-available/ms51-connect.before-tunnel-20261116`.
- `nginx -t` passed; configuration reloaded without restarting the VPS.
- Existing `/ms51/connect/v1/admin/` returned HTTP 200 afterward.
- Verified public HTTPS against the same Root X1 used by the bench firmware.
- MQTT discovery found `b43a45cf4b14` without a seeded device/root record.

## Verification

Python tests passed: 18 portal, 7 SDK, 6 HTTPS; native `fota_protocol` passed.
Build SHA-256: `23b38489635db2217492fbafcc25499c8d37263bc2a66a5527d6d3dcf6732060`.
The public download returned HTTP 200, exact Content-Length 1,012,016 bytes,
no chunked encoding, and the same SHA-256.

USB bootstrap passed on COM5, MAC `b4:3a:45:cf:4b:14`. App0 was backed up,
flash verified, and bootloader/partition/NVS bytes were unchanged. MQTT confirmed
running version 1162 and the public URL prefix after restart.

Physical reinstall job `a1b6a94de2184d5cae6f3429659dca13` was accepted but failed
with `https_connect` at 0 bytes. No automatic retry was issued. Version 1162
remained running; this is NOT a completed OTA. Follow-up firmware adds detailed
TLS/socket diagnostics so the failure can be identified without guessing.
Power-loss rollback is not yet qualified.
