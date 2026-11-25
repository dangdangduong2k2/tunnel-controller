# Tunnel MQTT on the VPS — 2026-11-17

The dedicated Tunnel broker is `103.9.78.139:1883` (MQTT TCP, username/password).
The existing firmware and SDK use plain MQTT TCP; this is not the MS51 TLS
listener on port 8884. No firmware flash is required to edit the saved MQTT
settings through the HMI.

## HMI setup

Open **Settings → MQTT**, enable MQTT, and enter:

| Field | Value |
|---|---|
| Host | `103.9.78.139` |
| Port | `1883` |
| Username | `tunnel` |
| Password | See the local ignored `private/MQTT_HMI_SETTINGS.md` handoff |
| Topic root | `tunnel/test/board-7c93a1/v1` (keep unchanged) |

When the machine is idle, select **Apply changes**, then confirm **Apply and
restart?**. The board stores the configuration in NVS and restarts. Confirm
MQTT connected on HMI and the actual device appearing Online in
https://103-9-78-139.sslip.io/tunnel/admin/ . Do not infer connection from old
OTA history: discovery is scoped to the selected broker, with fresh heartbeats.

The board account may read/write its own root and announce discovery only as
`b43a45cf4b14`. A different root or board needs an appropriate broker account/ACL.
SDK applications using this same board should also change host, port and MQTT
credentials; API topics and command payloads are unchanged.

## Server

- Service: `tunnel-mqtt.service`, unprivileged user `tunnel-mqtt`.
- Listener binds public IPv4 `103.9.78.139:1883`; UFW allows this on eth0.
- Configuration/hashed passwords/ACL: `/etc/tunnel-mqtt/`.
- Persistent broker state: `/var/lib/tunnel-mqtt/`.
- Anonymous clients are refused. The OTA portal has a separate `tunnel-ota`
  broker account, stored only in `/etc/tunnel-ota/server.json` and the local
  ignored credential file. Credentials do not belong in Git or release packages.
- The OTA portal now connects to the VPS broker and subscribes for discovery.
  The HTTPS download prefix and release signing key remain unchanged.
- Existing localhost Mosquitto, MS51 services and TLS port 8884 were preserved.

Before migration, the portal configuration was backed up to
`/etc/tunnel-ota/server.json.before-mqtt-vps-20261117` and SQLite to
`/var/lib/tunnel-ota/portal.before-mqtt-vps-20261117.sqlite3`. No active OTA jobs
were present. Restoring broker configuration requires restarting only
`tunnel-ota.service`; the board must use the matching broker as well.

## Verification and remaining step

External PC tests passed: valid board/portal login, QoS 1 exchange both ways on
a unique diagnostic topic, refusal of anonymous and incorrect-password clients,
and refusal of cross-board publishing. No test discovery or machine command was
published. The portal reports the VPS broker with discovery connected, while
MS51 still returns HTTP 200.

The real board was offline during migration and its HMI configuration has not
been changed remotely. Actual board connection and OTA through this new broker
remain to be verified after the HMI settings above are applied.
