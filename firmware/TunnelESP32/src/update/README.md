# FOTA protocol

`FotaProtocol` parses the bounded signed envelope, builds the canonical signed
bytes, validates target/version/size/URL/session/token, and tracks replay IDs.
It has no motor, HTTP, flash or private-key access. Native tests exercise this
code and the Modbus write inhibit independently of ESP hardware.

`FotaTrust.h` defaults to disabled FOTA. Local provisioning supplies only a
public P-256 signing key, trusted HTTPS URL prefix and optional test CA through
the ignored `FotaTrust.local.h`. Private keys must never enter firmware or Git.

See [FOTA integration and limitations](../../../../docs/FOTA.md).
