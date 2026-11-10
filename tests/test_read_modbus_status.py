import importlib.util
import pathlib
import socket
import struct
import threading
import unittest

path = pathlib.Path(__file__).resolve().parents[1] / "tools/read_modbus_status.py"
spec = importlib.util.spec_from_file_location("diagnostic", path)
diagnostic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostic)


class DiagnosticTests(unittest.TestCase):
    def test_known_crc(self):
        self.assertEqual(diagnostic.crc16(bytes.fromhex("01 03 00 00 00 0A")), 0xCDC5)

    def test_decode(self):
        r = [0] * 32
        r[0] = 1
        r[1] = 170
        r[25] = 0xC0A8
        r[26] = 0x0132
        r[11] = 1
        r[12] = 2
        status = diagnostic.decode(bytes([4, 64]) + struct.pack(">32H", *r))
        self.assertEqual(status["ip"], "192.168.1.50")
        self.assertEqual(status["total_reads"], 65538)

    def test_exception(self):
        with self.assertRaises(RuntimeError):
            diagnostic.decode(bytes([0x84, 6]))

    def test_fragmented_tcp_read_only(self):
        server = socket.socket()
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        port = server.getsockname()[1]
        received = []

        def worker():
            with server:
                stream, _ = server.accept()
                with stream:
                    stream.settimeout(2)
                    received.append(diagnostic.exact(stream, 12))
                    pdu = bytes([4, 64]) + struct.pack(">32H", *range(32))
                    response = struct.pack(">HHHB", 1, 0, len(pdu) + 1, 7) + pdu
                    for byte in response:
                        stream.sendall(bytes([byte]))

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()
        status = diagnostic.read_tcp("127.0.0.1", port, unit_id=7)
        thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertEqual(status["state"], 2)
        self.assertEqual(received, [bytes.fromhex("00 01 00 00 00 06 07 04 00 00 00 20")])


if __name__ == "__main__":
    unittest.main()
