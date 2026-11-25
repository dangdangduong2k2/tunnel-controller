"""Create local HTTPS test credentials and a public-only firmware trust header."""

import argparse
import datetime as dt
import ipaddress
import json
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

ROOT = Path(__file__).resolve().parents[2]


def create(host, output, header):
    output = Path(output)
    header = Path(header)
    if output.exists() or header.exists():
        raise ValueError("Use a new output directory/header; existing trust is never overwritten")
    ip = ipaddress.ip_address(host)
    output.mkdir(parents=True)
    now = dt.datetime.now(dt.timezone.utc)
    ca_key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Tunnel local FOTA test CA")])
    ca = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(ca_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - dt.timedelta(days=1))
        .not_valid_after(now + dt.timedelta(days=365))
        .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
        .add_extension(
            x509.KeyUsage(False, False, False, False, False, True, True, False, False),
            critical=True,
        )
        .sign(ca_key, hashes.SHA256())
    )
    server_key = ec.generate_private_key(ec.SECP256R1())
    cert = (
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, host)]))
        .issuer_name(name)
        .public_key(server_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - dt.timedelta(days=1))
        .not_valid_after(now + dt.timedelta(days=90))
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(
            x509.SubjectAlternativeName(
                [
                    x509.IPAddress(ip),
                    x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
                    x509.DNSName("localhost"),
                ]
            ),
            critical=False,
        )
        .add_extension(
            x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.SERVER_AUTH]), critical=False
        )
        .sign(ca_key, hashes.SHA256())
    )
    signing_key = ec.generate_private_key(ec.SECP256R1())
    for label, key in (("ca", ca_key), ("server", server_key), ("release", signing_key)):
        (output / (label + ".key")).write_bytes(
            key.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            )
        )
    ca_pem = ca.public_bytes(serialization.Encoding.PEM).decode()
    public = (
        signing_key.public_key()
        .public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
        .decode()
    )
    (output / "ca.pem").write_text(ca_pem, encoding="ascii")
    (output / "server.pem").write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    (output / "release-public.pem").write_text(public, encoding="ascii")
    prefix = f"https://{host}:8443/firmware/"
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text(
        "#pragma once\n// PUBLIC data only. Private keys remain outside firmware.\n"
        + "#define TUNNEL_FOTA_URL_PREFIX "
        + json.dumps(prefix)
        + "\n"
        + 'static constexpr char TUNNEL_FOTA_CA_PEM[] = R"FOTACA('
        + ca_pem
        + ')FOTACA";\n'
        + 'static constexpr char TUNNEL_FOTA_PUBLIC_KEY[] = R"FOTAPUB('
        + public
        + ')FOTAPUB";\n',
        encoding="ascii",
    )
    return prefix


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True, help="LAN IPv4 address of this PC")
    p.add_argument("--output", type=Path, default=ROOT / "private/fota-test")
    p.add_argument(
        "--header", type=Path, default=ROOT / "firmware/TunnelESP32/src/update/FotaTrust.local.h"
    )
    a = p.parse_args()
    print("Provisioned test URL prefix:", create(a.host, a.output, a.header))
    print("Private keys saved locally; never distribute the private directory.")
