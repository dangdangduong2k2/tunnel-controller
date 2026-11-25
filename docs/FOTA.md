# FOTA 1.16.4 — public HTTPS deployment

Version 1.16.4 fixes the measured TLS allocation failure by releasing idle
reader deduplication storage. See [current results](RELEASE_1_16_4.md).

The manufacturer console is deployed at
https://103-9-78-139.sslip.io/tunnel/admin/ . Version 1.16.2 provisions the bench
board for the public download prefix and ISRG Root X1, preserving the release
verification key. See [current deployment and hardware results](RELEASE_1_16_2.md).

Since 1.16.1, a fresh signed request can upgrade, reinstall the same version or
downgrade a compatible image. The SDK and portal apply the same policy. Version
must still match the selected binary; this does not relax signature, target,
freshness, ownership or image checks. See [release notes](RELEASE_1_16_1.md).

This source implements MQTT-triggered HTTPS application OTA. A physical public
HTTPS reinstall and post-boot confirmation passed with 1.16.4 on 2026-11-16;
deliberate power-loss rollback remains unverified. See
[1.16.0 changes, operation and test cases](OTA_1_16_0.md).
The rebuild fixes cancellation by the update's own STOP fence, adds embedded
image identity, key/URL preflight and bounded post-boot health confirmation.

Historical bench observation: firmware 1.13.0 was installed over USB on 2026-11-11,
with flash verification and bootloader/partition/NVS preservation confirmed.
After LAN reconnection, hardware b43a45cf4b14 announced firmware 1130 via
MQTT discovery and reported FOTA configured/storage_ready=true, state=idle.
The local portal on port 8091 serves HTTPS downloads from the existing test
CA at 192.168.1.18:8443. Physical OTA download and power-loss tests remain
unverified; do not confuse server-side HTTPS checks with a successful board OTA.

The simple manufacturer upload/deploy console runs on the VPS, with no login
and MQTT board discovery. See [portal setup and discovery](../tools/fota/PORTAL.md).
The source also provides a signed Python client and a local HTTPS test harness.
DWIN assets, bootloader, partition table and filesystem are outside OTA scope.

## Wire contract

FOTA topics are under the configured root. The existing `/cmd`, `/get`, `/state`
and `/ack` are unchanged. FOTA has its own bounded queues and topics, preventing
firmware metadata from consuming ordinary control command slots.

| Topic | Direction | Payload |
|---|---|---|
| `/ota/cmd` | Client → board | Signed flat JSON envelope, at most 1536 bytes, retain=false |
| `/ota/ack` | Board → client | job, client, id, state (`accepted`, `duplicate`, `rejected`), error |
| `/ota/get` | Client → board | Exactly `{}`, retain=false; query only |
| `/ota/state` | Board → clients | Latest job, state, error, bytes, total, percent, running/target version, configured/storage flags; revision 2 also reports url_prefix, signing_key_id, image_schema |

FOTA state is not retained and is a device-wide snapshot, not a correlated RPC
reply. Query again after reconnect/reboot. A command ACK only reports admission;
`accepted` and `percent=100` never mean boot success. Public test broker messages
are not authenticated telemetry; production requires private broker access/ACLs.

Envelope fields: `job` (32 lowercase hex), `session` and `client` (16 lowercase
hex), `epoch`, positive `id`, `seen_ms`, HTTPS `url` (max 256 ASCII characters),
`target="tunnel-esp32s3-v1"`, `version` (firmware register integer), `size`,
`sha256` (64 lowercase hex), `unix_time`, and `signature` (hex DER P-256 ECDSA).
Unknown/duplicate fields, nesting, escaped strings and incomplete messages are
rejected. Freshness is checked against the same MQTT session/epoch and the
5000 ms token policy. No automatic client reclaim or command retry. Downgrade is
allowed when explicitly selected and signed, never chosen automatically by the client.

The exact signed bytes are ASCII, with a final newline:

```text
TUNNEL-FOTA-1
job
session
epoch
client
id
seen_ms
url
target
version
size
sha256
unix_time
```

Each line after the magic contains its actual field value; integers are decimal
without padding. SHA-256 of these bytes is signed using ECDSA P-256. The private
release key remains with the manufacturer/test operator. Session tokens alone
are not update authorization. An authenticated fresh request can bootstrap an
unset clock from `unix_time` for TLS certificate validation; it does not change
an already initialized clock.

## Device flow and interlocks

1. A low-priority worker parses and verifies signatures. The control loop then
   checks live ownership, motion, RF, reader transaction, configuration job and
   eligible machine state. Waiting Semi results and active cycle states are busy.
2. The accepted job reserves an update lock, requests a STOP fence, and waits
   for queued control input to be discarded before beginning the download.
3. All Modbus writes except STOP/Emergency are inhibited; reads remain available.
   Local HMI/legacy ordinary execution is blocked. Configuration commits are
   denied while the update lock is held. The job does not require a motion lease.
4. HTTPS uses a trusted CA or the ESP certificate bundle, exact configured URL
   prefix, no redirects, HTTP 200 and exact Content-Length. The first version
   rejects chunked responses. Socket timeout is 3 seconds; download time budget
   is 120 seconds. It writes only the inactive application slot.
5. The worker checks the signed hash, embedded target/version, image/chip validity and cancellation before
   changing boot selection. Progress is coalesced to roughly once per second;
   phase changes are also emitted. MQTT interruption does not resume motion or
   automatically restart the download.
6. The next boot defers Arduino's default early OTA confirmation. Runtime,
   config storage, motor readiness and stopped RF/motors must remain healthy
   for at least 5 seconds, after boot uptime exceeds 10 seconds. The running
   firmware register must match the signed job. Otherwise rollback is requested
   after 30 seconds. Broker connectivity is not a boot-health requirement.
7. A small NVS job record enables post-reboot status. Failures and healthy new
   boot completion request another STOP/input fence before unlocking. There is
   no automatic Scan, reset/homing or conveyor start after an update.

## Provisioning and rollout

See [local test commands](../tools/fota/README.md). Trust defaults to disabled
without the local header. The test build pins a local URL prefix/CA. Moving to
a production domain currently requires reprovisioning that trust and rebuilding
once. Later signed releases under that trusted domain/prefix only need a new
file/URL and signed command. Runtime trust/key rotation is not implemented.

Do not assume the installed bootloader matches the selected Arduino core:
verify the physical partition/bootloader and make a recovery backup before the
first USB bootstrap. Two OTA slots exist in the selected `default_8MB` build,
but source configuration alone does not prove the physical rollback path.

## Verification boundaries

Native coverage: actual parser/canonicalization, guards, replay cache and shared
Modbus inhibition. Local integration: actual TLS server/certificates and Python
signatures, not an emulated ESP flash driver. Existing control/config/MQTT suites
are rerun. This does **not** prove ESP TLS heap usage, real flash behavior, boot
rollback, progress under LAN loss, emergency timing during flash, or power-cut
recovery. Those remain hardware qualification gates; do not label them PASS.

Physical tests must verify configuration and HMI history preservation as well as
application update. OTA writes the inactive app slot; this is not a migration
system for configuration/history schemas changed by future application code.

References: [ESP-IDF OTA](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/system/ota.html),
[ESP HTTPS OTA](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/system/esp_https_ota.html).
