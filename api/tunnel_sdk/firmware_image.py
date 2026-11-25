"""Inspect a Tunnel ESP32-S3 application; no signing, flashing or network access."""

import hashlib
import struct

LIMIT = 0x330000
TARGET = "tunnel-esp32s3-v1"
MAGIC = b"TUNNEL-OTA-V1".ljust(16, b"\0")
IDENTITY = struct.Struct("<16sII32s")


def inspect_firmware(data):
    if not 256 <= len(data) <= LIMIT or data[0] != 0xE9:
        raise ValueError("Chọn application .bin của Tunnel, tối đa 3.19 MiB.")
    if struct.unpack_from("<H", data, 12)[0] != 9 or not 1 <= data[1] <= 16:
        raise ValueError("Firmware không dành cho ESP32-S3.")
    if data[32:36] != bytes.fromhex("3254cdab"):
        raise ValueError("Cần app.bin, không phải bootloader hoặc merged.bin.")
    pos, checksum = 24, 0xEF
    segments = []
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError("Firmware bị thiếu dữ liệu.")
        size = struct.unpack_from("<I", data, pos + 4)[0]
        pos += 8
        if pos + size > len(data):
            raise ValueError("Firmware bị thiếu dữ liệu.")
        segments.append((pos, pos + size))
        for b in data[pos : pos + size]:
            checksum ^= b
        pos += size
    end = (pos // 16 + 1) * 16
    if end > len(data) or data[end - 1] != checksum:
        raise ValueError("Checksum firmware không đúng.")
    if data[23] != 1 or len(data) != end + 32 or hashlib.sha256(data[:end]).digest() != data[end:]:
        raise ValueError("SHA-256 hoặc chiều dài firmware không đúng.")
    positions = []
    start = 0
    while True:
        at = data.find(MAGIC, start)
        if at < 0:
            break
        positions.append(at)
        start = at + 1
    if len(positions) != 1 or not any(
        a <= positions[0] and positions[0] + IDENTITY.size <= b for a, b in segments
    ):
        raise ValueError(
            "Thiếu hoặc trùng nhận diện Tunnel OTA. Dùng app.bin được build từ 1.16.0 trở lên."
        )
    _, schema, version, target = IDENTITY.unpack_from(data, positions[0])
    if schema != 1 or version < 1160 or target != TARGET.encode().ljust(32, b"\0"):
        raise ValueError("Nhận diện firmware Tunnel không hợp lệ hoặc sai dòng bo.")
    return dict(
        schema=schema,
        version=version,
        target=TARGET,
        size=len(data),
        sha256=hashlib.sha256(data).hexdigest(),
    )
