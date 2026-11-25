# Local FOTA tools

`portal.py` provides the simple no-login manufacturer upload/deploy page.
See [PORTAL.md](PORTAL.md) for launch, server configuration and limitations.
The other tools below are the development harness.
Install the SDK `fota` extra, or install `paho-mqtt` and `cryptography` locally.

1. `provision.py --host 192.168.1.18` creates separate signing and HTTPS keys in
   ignored `private/fota-test/`, and a **public-only** local firmware header.
   It refuses to replace existing trust. Use a new output/header deliberately
   when rotating keys or changing the test address. No eFuse is changed.
2. Build the bootstrap FOTA firmware and validate it before USB flashing.
3. Build a newer compatible application image; copy that `.bin` into the served
   directory. Do not use a merged bootloader/partition/full-flash image.
4. Start the server explicitly for LAN access:

```powershell
python tools/fota/https_server.py --bind 192.168.1.18 --port 8443 --directory dist --cert private/fota-test/server.pem --key private/fota-test/server.key
```

5. Query update state (no claim or motion):

```powershell
python tools/fota/client.py status --host broker.emqx.io --root tunnel/test/board-7c93a1/v1
```

6. Submit a newer image. This sends one signed command; there is no automatic
   retry if the ACK is missing. The example target `1161` requires a real newer
   build with embedded firmware register `1161`; build and qualify that release
   before using this command. The bootstrap release in this source is `1160`.

```powershell
python tools/fota/client.py update --host broker.emqx.io --root tunnel/test/board-7c93a1/v1 --url https://192.168.1.18:8443/firmware/TunnelESP32_v1_16_1_app.bin --firmware dist/TunnelESP32_v1_16_1_app.bin --version 1161 --signing-key private/fota-test/release.key
```

The PC must be reachable from the board on the LAN. Local CA/server keys are
test credentials, not a production certificate management system. No firewall
rule, server service, public upload, board flash, or broker change is performed
by provisioning/build/tests. The HTTPS server defaults to localhost unless
`--bind` is provided. It serves only `.bin` files and has no upload endpoint.
