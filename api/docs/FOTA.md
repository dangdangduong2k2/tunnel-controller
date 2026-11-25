# FOTA client (firmware 1.16.1 candidate / SDK 0.1.5)

Install `pip install "tunnel-iot-python-sdk[fota]"` from the provided local
package. This feature is a development candidate, not hardware-qualified yet.

```python
from tunnel_sdk import FotaClient

with FotaClient("broker.emqx.io", root="tunnel/test/board-7c93a1/v1", timeout=8) as device:
    print(device.get_update_status())
```

To submit a signed release, use `update_firmware(url=..., firmware=..., version=...,
signing_key=...)`. `firmware` is the local application `.bin` used to calculate
size/hash; the same bytes must already be available at the URL. `signing_key`
is a local P-256 private key matching the public key installed on the board.
`version` is the target integer firmware register, e.g. 1161 for 1.16.1. It must
match the embedded identity in the actual app.bin (build 1.16.0+). The SDK checks
the complete image before publishing; renaming the file does not change version.

Install 1.16.0 by USB once before using this OTA revision. Legacy OTA revisions
are rejected because they lack the corrected update fence. The SDK reads
`/ota/state` to check `configured`, `storage_ready`, `ota_revision >= 2`,
`url_prefix` and `signing_key_id`, then obtains fresh control-session state.
The public-key fingerprint must match the selected server signing key, and the
URL must be under the board's provisioned prefix. These checks never claim
control or start motion.

The return value is the admission ACK, not completion. `rejected` includes an
error string. After `accepted`, observe `/ota/state` or query `get_update_status()`.
A missing ACK raises `UnknownOutcome`; do not automatically resend or claim
control. The machine must be idle/unowned. A fresh signed job may upgrade,
reinstall the same version or downgrade a compatible application. The version
must match the actual image, but need not exceed the running version. The running
firmware must have the 1.16.1 admission-policy fix to accept reinstall/downgrade;
1.16.0 can still upgrade to this release.

Upload/deploy dashboard and firmware hosting are separate components. These
methods neither upload files to a server nor flash through USB. MQTT is for
commands/status; the board downloads the binary over HTTPS.

The broker test setup does not authenticate telemetry; a private broker with
per-device ACLs is required for production. HTTPS URL trust is provisioned in
firmware; arbitrary URLs are not accepted even when reachable from a browser.
