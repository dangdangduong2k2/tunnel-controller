# 1.16.4 — RAM available for HTTPS OTA

## Diagnosed failure

The 1.16.3 diagnostic build reported `http_error=28674` (`0x7002`),
`tls_error=32791` (`0x8017`, SSL setup failed), and `tls_code=32512`
(`0x7F00`, SSL allocation failed). Free heap before download was 32,040 bytes;
the largest block was 21,492 bytes. The failure occurred before image writes.

The static reader deduplication table occupied 66,560 bytes even when no scan
was active. Version 1.16.4 reserves this storage in 32-tag chunks before scan
START and releases it in the owner task after completion/cancellation. It keeps
the same 1,024-tag capacity and 64-byte EPC limit. Pause/resume retains the table;
completed count, total reads, last EPC and persistent history remain available.
Allocation failure refuses START, releases partial reservations and preserves
previous counts. No RF command is transmitted in that failure path.

Public HTTPS trust and signing key are unchanged from 1.16.2. Signature,
identity/hash, freshness, duplicate protection, BUSY gating and the safety fence
remain enforced. USB bootstrap is needed for boards still running a build with
the memory issue or private-PC download trust.

## Diagnostic fields

`/ota/state` adds `http_error`, `tls_error`, `tls_code`, `tls_flags`,
`socket_errno`, `heap_before`, and `heap_largest`. These describe the latest
attempt in the current boot and reset on reboot; they do not change the NVS
journal layout. SDK clients that ignore additional JSON fields remain compatible.

## Validation

The complete native regression suite passed, including actual reader/control
integration, MQTT, Modbus, HMI, history and output safety. Reader tests exercise
all 1,024 maximum-length EPCs, duplicate handling across pause/resume, timeout,
cancel, disconnected USB, late frames after release, and allocation failure at
every reservation step. The existing Python portal/SDK/HTTPS suites passed
(18 + 7 + 6 tests) during the public-server deployment.

Physical tests passed on 2026-11-16, board MAC `b4:3a:45:cf:4b:14`:

- USB bootstrap on COM5: backed up app0, verified the written app, and confirmed
  unchanged bootloader/partition/NVS bytes before restart.
- Binary: `TunnelESP32_v1_16_4_app.bin`, 1,012,544 bytes.
- SHA-256: `12acb9bbf389d6a6b7db15b43a23242fde3e39ab59c8538fa00b032e77a39178`.
- The public HTTPS download returned exact length and hash, without chunking.
- Reinstall job `39c0055d3b0544309dc1159a491f5d7c`: accepted, downloading (64%
  sampled), rebooting at 100%, boot_verifying, then success. Exactly one command
  submission; no automatic retry.
- An independent status query confirmed the same job, state `success`, empty
  error, running/target version 1164, storage ready, owner=0 and activity=0.
- Board IP was `192.168.0.122`, while the PC was `192.168.1.18`; the firmware
  came from the public VPS HTTPS URL, not the PC. Shared LAN access is unnecessary.
- Generated-file, Python syntax, project structure and local Markdown-link checks passed.

This validates public HTTPS download, verified image installation, same-version
reinstall and post-boot confirmation. Deliberate power-loss rollback and a physical
downgrade test were not performed in this run. The two earlier diagnostic builds
(1.16.2/1.16.3) retain the RAM defect; use 1.16.4 for continued OTA testing.
