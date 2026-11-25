# Tunnel firmware console

Deployment update, 2026-11-17: the VPS console now uses the authenticated broker
`103.9.78.139:1883`, with its own portal account. Existing boards/SDK clients must
use the matching MQTT settings. See [VPS MQTT and HMI setup](../../docs/MQTT_VPS.md).
The table below still describes source environment-variable defaults, not the
overrides in the deployed server configuration.

One page, no account or login (requested). Upload app.bin → select release →
confirm update → see progress/history. The device picker is populated only from
MQTT discovery announcements; there are no seeded devices or default device root.
No automatic update after upload. Upload checks ESP32-S3 application structure,
checksum, digest and embedded Tunnel identity. Version and target are read from
the binary automatically; there is no manual version field. Use a genuine
release app.bin built from 1.16.0 or newer. Renaming a file cannot change its
version. Identity validation prevents mistakes; it is not code attestation.

Legacy boards require a one-time USB bootstrap (use the public-trust 1.16.4 build).
Earlier local-trust/RAM-constrained builds also need that bootstrap. The corrected OTA
protocol is revision 2; the portal and SDK block legacy firmware. After bootstrap,
select a compatible release. Firmware 1.16.1 permits newer, same and older
versions; the existing confirmation identifies the action and before/after
versions. There is no additional downgrade password or permission switch.
The pre-identity binaries from before 1.16.0 are still incompatible with this
OTA format and require USB flashing. This is separate from version ordering.

## Run locally

For an already-provisioned bench (same PC address, CA and signing key as the
firmware), run both services with one command:

```powershell
python tools/fota/serve_local.py --host 192.168.1.18
```

Open http://127.0.0.1:8092/tunnel/admin/ . The launcher checks the source trust
header against the test URL/key; actual board trust is checked again by the SDK.
Only firmware downloads bind the specified LAN IP. The admin page stays on
loopback. This does not change a firewall or deploy anything on the VPS.

From the repository root:

```powershell
python -m pip install -r tools/fota/requirements-portal.txt
python tools/fota/portal.py
```

Open http://127.0.0.1:8090/tunnel/admin/ . SQLite and uploaded binaries are stored
in ignored `private/fota-portal/`. The service binds loopback by default. No
firmware, accounts, keys or demo update records are preloaded. Up to 100 MiB of
firmware and the latest 100 jobs are displayed; job records persist in SQLite.

## Server configuration

Set environment variables before starting the service:

Alternatively copy `server.example.json` to ignored `private/fota-server.json`,
set its broker, HTTPS prefix and private-key path, then run:

```powershell
python tools/fota/portal.py --config private/fota-server.json
```

The page reports missing/invalid configuration. A valid existing P-256 key is
required, not just a file at that path. Before dispatch, the SDK checks the
board's OTA revision 2 URL prefix and public-key fingerprint against the server.
Uploading a file never provisions the board's trust or deploys a web server.

| Variable | Meaning / default |
| --- | --- |
| TUNNEL_MQTT_HOST | `broker.emqx.io` |
| TUNNEL_MQTT_PORT | `1883` (current SDK uses MQTT TCP, not TLS) |
| TUNNEL_MQTT_USER / TUNNEL_MQTT_PASSWORD | Optional broker credentials, never returned to browser |
| TUNNEL_FIRMWARE_BASE | Exact HTTPS download prefix, ending `/` |
| TUNNEL_SIGNING_KEY | Absolute path to P-256 release private key |

Public prefix deployed on 2026-11-16:
`https://103-9-78-139.sslip.io/tunnel/firmware/`.
Console: `https://103-9-78-139.sslip.io/tunnel/admin/`.
Board trust must allow that exact prefix and validate its HTTPS certificate
chain and release public key. The 1.16.2 bench build uses ISRG Root X1 and
the existing release public key. Local-test builds trusting `192.168.1.18`
need a one-time USB bootstrap to switch to the public server.
See [deployment record](../../docs/RELEASE_1_16_2.md) and
[RAM fix and latest hardware results](../../docs/RELEASE_1_16_4.md).

The VPS runs `tunnel-ota.service` as the dedicated `tunnel-ota` user. Application
code lives in `/opt/tunnel-ota/app`, Python in `/opt/tunnel-ota/venv`, and persistent
SQLite/uploads in `/var/lib/tunnel-ota`. Configuration and the separately
transferred signing key are under `/etc/tunnel-ota` (root-owned, group-readable).
The backend binds only `127.0.0.1:8090`; Nginx exposes the two `/tunnel/` paths.
Existing MS51 routes and TLS renewal configuration are preserved. Restarting the
portal marks active jobs unknown and requires reconciliation, never a resend.

Run one portal process. Behind the existing Nginx HTTPS server, add these
locations **inside the existing sslip.io server block**; preserve MS51 routes,
certificates and other services:

```nginx
location /tunnel/admin/ {
    client_max_body_size 4m;
    proxy_pass http://127.0.0.1:8090;
    proxy_set_header Host $host;
    proxy_read_timeout 60s;
}
location /tunnel/firmware/ {
    proxy_pass http://127.0.0.1:8090;
    proxy_set_header Host $host;
}
```

No login means anyone who can reach the admin URL can upload and trigger updates.
Same-origin checks prevent browser form forgery, not unauthorized direct API use.
The signing key remains server-side; it does not provide human access control.
Use the requested network exposure deliberately. Firmware download URL must be
reachable by the board. Public test-broker telemetry is not authenticated.

## Automatic device discovery

The portal subscribes to `tunnel/discovery/v1/+` on its configured broker.
Each compatible board publishes on connect, reconnect and every 15 seconds:

```text
tunnel/discovery/v1/<12-lowercase-hex-MAC>
```

```json
{"discovery_version":1,"device_id":"112233445566","model":"tunnel-esp32s3-v1","root":"tunnel/112233445566/v1","firmware":1130}
```

This example is documentation only, never preloaded into the page or published
by the portal. Firmware derives identity from the factory Wi-Fi STA MAC via
`esp_read_mac`, even when it connects using Ethernet. The `root` field reports
the board's actual configured MQTT root; existing custom roots are preserved.
Each board must have a distinct root. If two discovered IDs advertise the same
root, the page flags the conflict and blocks status/deploy routing for them.

Discovery is QoS 1, retained. Retained identity adds a *known* device but never
establishes Online. Only a directly received heartbeat makes it Online; it expires
after 45 seconds. The portal also subscribes to each announced root's existing
`/availability` Last Will; `online:false` immediately invalidates live presence.
Retained `online:true` is deliberately not treated as a live heartbeat. Losing
the portal's broker connection or restarting the portal invalidates all live
presence until fresh board heartbeats arrive. Persisted devices stay visible as
known/non-live records; they are not silently presented as currently connected.

The existing board firmware must be updated to the discovery-enabled build and
connected to LAN/broker before it can appear. This is MQTT announcement discovery,
not a LAN/IP scan or a broker client-list API. No motion/control command is sent
by discovery. Status and deployment always require a selected discovered ID;
the server resolves its root and records the ID with each update job. A job's
root is fixed at creation and checked again immediately before dispatch.

The listener accepts only bounded Tunnel metadata, caps the registry at 256
devices per broker, and isolates stored devices by broker host/port. These limits
are for the simple console, not a claim of unlimited fleet support. Identity is
self-reported over MQTT, not cryptographic device authentication. Production
broker ACLs must restrict who can publish each board's discovery/availability
topics. This version uses the existing TCP MQTT SDK (no MQTT TLS support).

## Results and recovery

Only a matching job with `success` and the expected running version is displayed
as successful. Accepted ACK is not completion. Timeout/restart leaves the job
`unknown`, blocks another update and **never automatically resends**. Click
**Kiểm tra bo** to reconcile its saved FOTA status after reconnecting. If the
board never received the job, a technician must verify its actual firmware and
state before clearing the unresolved job in SQLite; no unsafe “retry anyway”
button is provided. Firmware with no FOTA support will still report ordinary
status when available, and shows FOTA status as unavailable.

## Tests

```powershell
python tests/test_fota_portal.py
python tests/test_fota_sdk.py
python tests/test_fota_https.py
python tests/test_discovery_broker.py
```

Portal tests use fake boards: page/no-login, secret exclusion, origin protection,
image integrity, upload/download, duplicate uploads, setup gate, duplicate clicks,
BUSY job exclusion, matching success, lost ACK and restart recovery. These are
not physical-board FOTA tests. Discovery tests also cover empty registry,
multiple IDs, root conflict, malformed messages, retained/live distinction and
45-second expiry. The broker integration uses a real **localhost-only** AMQTT
broker and a simulated board, including abrupt TCP loss that triggers actual
broker Last Will delivery. It requires the development dependency `amqtt`.
No flash or public-broker update command is sent by tests.
