import socket
import struct
import threading
import time
from .common import Commands, crc16, words
from .operations import OPERATIONS
from .errors import ProtocolError, UnknownOutcome
from .status import decode_status


class Modbus(Commands):
    def __init__(
        self,
        *,
        host=None,
        port=502,
        serial_port=None,
        baud=19200,
        parity="E",
        stop_bits=1,
        unit=1,
        timeout=1,
    ):
        if bool(host) == bool(serial_port):
            raise ValueError("Specify host OR serial_port")
        if not 1 <= unit <= 247:
            raise ValueError("Invalid unit")
        if (
            baud not in (1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200)
            or parity not in ("N", "E", "O")
            or stop_bits not in (1, 2)
        ):
            raise ValueError("Unsupported RS485 format")
        self.unit, self.tid, self.lock = unit, 0, threading.RLock()
        self.serial = bool(serial_port)
        self.silent_interval = max(0.00175, 3.5 * (9 + (parity != "N") + stop_bits) / baud)
        self.stream = None
        if self.serial:
            import serial

            self.stream = serial.Serial(
                serial_port,
                baud,
                bytesize=8,
                parity=parity,
                stopbits=stop_bits,
                timeout=timeout,
                write_timeout=timeout,
            )
        else:
            self.stream = socket.create_connection((host, port), timeout)
            self.stream.settimeout(timeout)

    def close(self):
        with self.lock:
            if self.stream:
                self.stream.close()
                self.stream = None

    def _exact(self, count):
        data = b""
        while len(data) < count:
            part = (self.stream.read if self.serial else self.stream.recv)(count - len(data))
            if not part:
                raise OSError("Incomplete response")
            data += part
        return data

    def _request(self, pdu):
        with self.lock:
            if self.stream is None:
                raise UnknownOutcome("Connection closed; create a new client and inspect status")
            try:
                if self.serial:
                    time.sleep(self.silent_interval)
                    self.stream.reset_input_buffer()
                    frame = bytes([self.unit]) + pdu
                    frame += struct.pack("<H", crc16(frame))
                    if self.stream.write(frame) != len(frame):
                        raise OSError("Partial RTU write")
                    self.stream.flush()
                    head = self._exact(2)
                    if head[0] != self.unit:
                        raise OSError("Wrong RTU unit")
                    if head[1] & 128:
                        frame = head + self._exact(3)
                    elif head[1] in (3, 4):
                        count = self._exact(1)
                        if count[0] > 250:
                            raise OSError("Invalid byte count")
                        frame = head + count + self._exact(count[0] + 2)
                    elif head[1] in (6, 16):
                        frame = head + self._exact(6)
                    else:
                        raise OSError("Unexpected function")
                    if crc16(frame):
                        raise OSError("RTU CRC mismatch")
                    result = frame[1:-2]
                else:
                    self.tid = (self.tid + 1) & 65535
                    self.stream.sendall(
                        struct.pack(">HHHB", self.tid, 0, len(pdu) + 1, self.unit) + pdu
                    )
                    tid, protocol, length, unit = struct.unpack(">HHHB", self._exact(7))
                    if (tid, protocol, unit) != (self.tid, 0, self.unit) or not 2 <= length <= 254:
                        raise OSError("Invalid MBAP response")
                    result = self._exact(length - 1)
                if result[0] == (pdu[0] | 128) and len(result) == 2:
                    raise ProtocolError("Modbus exception " + str(result[1]))
                if result[0] != pdu[0]:
                    raise OSError("Wrong function")
                if pdu[0] in (6, 16) and result != pdu[:5]:
                    raise OSError("Write response mismatch")
                return result
            except (OSError, ValueError, struct.error) as ex:
                self.close()
                raise UnknownOutcome(
                    "Transport failed; command outcome may be unknown: " + str(ex)
                ) from ex

    def _read(self, function, address, count):
        result = self._request(struct.pack(">BHH", function, address, count))
        if len(result) != count * 2 + 2 or result[1] != count * 2:
            self.close()
            raise ProtocolError("Invalid read length")
        return list(struct.unpack(">" + "H" * count, result[2:]))

    def _single(self, address, value):
        self._request(struct.pack(">BHH", 6, address, value))
        return dict(accepted=True, result=0)

    def status(self):
        return decode_status(self._read(4, 0, 96))

    def claim(self):
        return self._single(256, 1)

    def heartbeat(self):
        # Same Modbus register renews or claims. A timeout is still STOP-latched;
        # the application must never automatically reissue Reset after timeout.
        return self._single(256, 1)

    def release(self):
        return self._single(256, 0)

    def stop(self):
        return self._single(258, 1)

    def emergency(self):
        return self._single(257, 1)

    def execute(self, op, args=()):
        if type(op) is not int or op not in OPERATIONS.values():
            raise ValueError("Unsupported operation")
        values = words(args)
        with self.lock:
            sequence = (self._read(3, 272, 1)[0] + 1) & 65535
            pdu = struct.pack(">BHHB8H", 16, 272, 8, 16, sequence, op, *values)
            self._request(pdu)
            result = self._read(3, 280, 1)[0]
            return dict(accepted=result == 0, result=result, sequence=sequence)
