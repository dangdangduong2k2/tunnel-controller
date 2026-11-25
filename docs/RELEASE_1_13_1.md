# 1.13.1 — candidate for the OTA upgrade test

Prepared on 2026-11-12. Firmware version is `1.13.1`; the register value and
signed FOTA target version are `1131`. This build only advances the release
contract from 1.13.0; machine behavior, Modbus map, MQTT commands, discovery and
the FOTA implementation are unchanged.

Use the actual new `TunnelESP32_v1_13_1_app.bin`, not a renamed 1.13.0 file.
An OTA attempt requires an older FOTA-enabled build already running on the board.
Uploading a file to the portal does not start OTA.

The local bench build uses the same release public key and local test CA as the
USB-installed 1.13.0. Its allowed download prefix is
`https://192.168.1.18:8443/firmware/`. It is not a public-internet production build.
Private signing/server keys are not inside the firmware or distributed artifacts.

1.13.0 was verified over USB and subsequently reported discovery and FOTA status
on 2026-11-11. On 2026-11-12 the user reported flashing different code to the
board; its present contents/partition layout must be read before restoring
Tunnel. USB is currently unavailable, so no 1.13.1 hardware flash or physical
OTA result is claimed by these release notes.

## USB restoration on 2026-11-14

Installed 1.13.1 on the same bench board via COM20 at 115200 baud.
Bootloader/partition table matched the build. Backed up all 983040 bytes of
application sectors being overwritten, wrote app0 at 0x10000, verified flash,
and verified the first 64 KiB (bootloader, partitions, NVS, otadata) unchanged
before reset. DWIN UART updates resumed; Ethernet reported NO LINK and MQTT
status timed out. This confirms USB restoration, not a successful OTA test.
