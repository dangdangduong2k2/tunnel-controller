"""Public device API shared by TCP, RTU and MQTT (firmware 1.11+)."""

from copy import deepcopy
import math
import threading

from .common import named_words
from .errors import ProtocolError, ReaderError
from .modbus import Modbus
from .mqtt import Mqtt
from .operations import CATALOG, OPERATIONS


class TunnelDevice:
    """Configure without IO; connect explicitly or use a context manager.

    Command methods return acceptance dictionaries, not completion results.
    No automatic claim, heartbeat, retry, reconnect or motion on close.
    """

    def __init__(self, transport, **settings):
        if transport not in ("tcp", "rtu", "mqtt"):
            raise ValueError("transport must be tcp, rtu or mqtt")
        timeout = settings.get("timeout", 5.0 if transport == "mqtt" else 1.0)
        if (
            isinstance(timeout, bool)
            or not isinstance(timeout, (int, float))
            or not math.isfinite(timeout)
            or timeout <= 0
        ):
            raise ValueError("timeout must be a positive finite number")
        self.transport = transport
        self._settings = dict(settings, timeout=timeout)
        self._client = None
        self._lock = threading.RLock()

    @classmethod
    def tcp(cls, host, *, port=502, unit=1, timeout=1.0):
        return cls("tcp", host=host, port=port, unit=unit, timeout=timeout)

    @classmethod
    def rtu(cls, serial_port, *, baud=19200, parity="E", stop_bits=1, unit=1, timeout=1.0):
        return cls(
            "rtu",
            serial_port=serial_port,
            baud=baud,
            parity=parity,
            stop_bits=stop_bits,
            unit=unit,
            timeout=timeout,
        )

    @classmethod
    def mqtt(cls, host, *, port=1883, username="", password="", root="tunnel/01/v1", timeout=5.0):
        return cls(
            "mqtt",
            host=host,
            port=port,
            username=username,
            password=password,
            root=root,
            timeout=timeout,
        )

    def connect(self):
        """Open transport. Does not read, claim, reset or run the machine."""
        with self._lock:
            if self._client is not None:
                if not self.is_connected():
                    raise ProtocolError("Connection lost; close explicitly before reconnecting")
                return self
            factory = Mqtt if self.transport == "mqtt" else Modbus
            self._client = factory(**self._settings)
            return self

    def is_connected(self):
        """Local transport state only; does not prove the device is reachable."""
        with self._lock:
            if self._client is None:
                return False
            if self.transport == "mqtt":
                return self._client.connected and self._client.subscribed
            return self._client.stream is not None

    def close(self):
        """Close transport only; call release explicitly to relinquish control."""
        with self._lock:
            client, self._client = self._client, None
            if client is not None:
                client.close()

    def __enter__(self):
        return self.connect()

    def __exit__(self, *unused):
        self.close()

    def _invoke(self, method, *args):
        with self._lock:
            if self._client is None:
                raise ProtocolError("Call connect() or use 'with' before accessing the device")
            return getattr(self._client, method)(*args)

    @staticmethod
    def get_functions():
        """Return all 19 supported OPs and parameter ranges, without device IO."""
        return deepcopy(CATALOG)

    def get_status(self):
        """Read a fresh state; no claim, lease renewal or counter reset."""
        state = self._invoke("status")
        state.update(
            machine_state=state["state"],
            epc_count=state["unique_epcs"],
            speed_level=state["speed"],
            scan_result=state["reader_flags"] >> 8,
        )
        return state

    def get_epc_count(self):
        return self.get_status()["epc_count"]

    def claim(self):
        """Acquire control and STOP/HOLD; not a Reset/homing command."""
        return self._invoke("claim")

    def heartbeat(self):
        """Explicit lease renewal; application normally calls every 500 ms."""
        return self._invoke("heartbeat")

    def release(self):
        return self._invoke("release")

    def has_control(self):
        """Read ownership for this client; does not renew its lease."""
        return self._invoke("has_control")

    def stop(self):
        return self._invoke("stop")

    def emergency(self):
        return self._invoke("emergency")

    def send_command(self, method, **params):
        """Named Python API; numeric wire OP on all transports, including MQTT 1.11."""
        values = named_words(method, params)
        return self._invoke("execute", OPERATIONS[method], values)

    def execute(self, op, args=()):
        """Low-level numeric OP API; at most six uint16 parameters, zero padded."""
        return self._invoke("execute", op, args)

    def reset(self):
        return self.send_command("reset")

    def set_gate(self, close):
        return self.send_command("gate", close=close)

    def set_belt(self, run):
        return self.send_command("belt", run=run)

    def set_scan(self, run):
        return self.send_command("scan", run=run)

    def set_mode(self, mode):
        return self.send_command("mode", mode=mode)

    def confirm(self):
        return self.send_command("confirm")

    def rescan(self):
        return self.send_command("rescan")

    def set_speed(self, level):
        return self.send_command("speed", level=level)

    def set_maintenance(self, enabled):
        return self.send_command("maintenance", enabled=enabled)

    def get_reader_info(self):
        return self.send_command("reader.info")

    def get_antenna_power(self):
        return self.send_command("antenna.power.get")

    def set_antenna_power(self, ant1_dbm, ant2_dbm, ant3_dbm, ant4_dbm, *, ram_only):
        return self.send_command(
            "antenna.power.set",
            ant1_dbm=ant1_dbm,
            ant2_dbm=ant2_dbm,
            ant3_dbm=ant3_dbm,
            ant4_dbm=ant4_dbm,
            ram_only=ram_only,
        )

    def set_antenna_mask(self, mask, *, ram_only):
        return self.send_command("antenna.mask.set", mask=mask, ram_only=ram_only)

    def get_rf_profile(self):
        return self.send_command("reader.profile.get")

    def set_rf_profile(self, encoded):
        return self.send_command("reader.profile.set", encoded=encoded)

    def get_gen2x(self):
        return self.send_command("reader.gen2x.get")

    def set_gen2x(self, value):
        return self.send_command("reader.gen2x.set", value=value)

    def get_ex10(self):
        return self.send_command("reader.ex10.get")

    def measure_return_loss(self, frequency_khz, antenna):
        if (
            type(frequency_khz) is not int
            or not 840000 <= frequency_khz <= 960000
            or (frequency_khz % 100 and frequency_khz % 125)
        ):
            raise ValueError("frequency_khz: 840000..960000, divisible by 100 or 125")
        return self.send_command(
            "antenna.return_loss",
            frequency_hi=frequency_khz >> 16,
            frequency_lo=frequency_khz & 65535,
            antenna=antenna,
        )

    def get_reader_result(self, sequence, *, expected_command=None):
        """One status read. Return None while pending/unmatched, else validated reply.

        Caller schedules heartbeat and deadline. Reader rejection/transport failure
        raises ReaderError. Does not start a new reader command.
        """
        if type(sequence) is not int or not 0 <= sequence <= 65535:
            raise ValueError("sequence must be uint16")
        if expected_command is not None and (
            type(expected_command) is not int or not 0 <= expected_command <= 255
        ):
            raise ValueError("expected_command must be a byte")
        state = self.get_status()
        if state["reader_sequence"] != sequence or state["reader_state"] in (0, 1):
            return None
        if state["reader_state"] != 2 or state["reader_transport_status"]:
            raise ReaderError(f"Reader transport failed: {state['reader_transport_status']}")
        reply = state["reader_reply"]
        if len(reply) < 2 or any(type(b) is not int or not 0 <= b <= 255 for b in reply):
            raise ReaderError("Invalid reader reply")
        if expected_command is not None and reply[0] != expected_command:
            raise ReaderError("Unexpected reader command in reply")
        if reply[1] != 0:
            raise ReaderError(f"Reader status {reply[1]}")
        return dict(
            sequence=sequence,
            command=reply[0],
            status=reply[1],
            data=list(reply[2:]),
            raw_reply=list(reply),
        )
