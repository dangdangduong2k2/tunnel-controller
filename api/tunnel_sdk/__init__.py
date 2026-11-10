"""Tunnel SDK: explicit ownership and heartbeat; no automatic motion or retry."""

from .errors import BusyError, ProtocolError, UnknownOutcome, ReaderError, require_accepted
from .status import decode_status
from .common import crc16, words
from .operations import OPERATIONS
from .modbus import Modbus
from .mqtt import Mqtt
from .device import TunnelDevice

__version__ = "0.1.1"


def from_settings(settings):
    kind = settings["transport"]
    if kind == "tcp":
        return Modbus(
            host=settings["host"], port=settings.get("port", 502), unit=settings.get("unit", 1)
        )
    if kind == "rtu":
        return Modbus(
            serial_port=settings["serial_port"],
            baud=settings.get("baud", 19200),
            parity=settings.get("parity", "E"),
            stop_bits=settings.get("stop_bits", 1),
            unit=settings.get("unit", 1),
        )
    if kind == "mqtt":
        return Mqtt(
            settings["mqtt_host"],
            settings.get("mqtt_port", 1883),
            settings.get("mqtt_username", ""),
            settings.get("mqtt_password", ""),
            settings.get("mqtt_root", "tunnel/01/v1"),
        )
    raise ValueError("transport must be tcp, rtu or mqtt")


__all__ = [
    "BusyError",
    "require_accepted",
    "TunnelDevice",
    "ReaderError",
    "__version__",
    "Modbus",
    "Mqtt",
    "ProtocolError",
    "UnknownOutcome",
    "decode_status",
    "crc16",
    "words",
    "OPERATIONS",
    "from_settings",
]
