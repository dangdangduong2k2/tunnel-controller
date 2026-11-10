"""Localhost TCP + simulated RTU serial; production Modbus core.
Requires requirements.txt and RUN_TESTS.cmd protocol_sim.
Never opens a physical COM port or connects to an external device.
"""

import contextlib
import logging
import pathlib
import socketserver
import struct
import subprocess
import sys
import threading
import time
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "api"))
from tunnel_api import Modbus, ProtocolError, crc16
from tunnel_sdk import BusyError, TunnelDevice

sys.path.insert(0, str(ROOT / "api/example"))
from reader_power import read_power
from device_console import Console


class Native:
    def __init__(self):
        self.process = subprocess.Popen(
            [str(ROOT / "test_build/protocol_sim/protocol_sim.exe"), "--serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.lock = threading.RLock()
        self.started = time.monotonic()

    def line(self, message):
        self.process.stdin.write(message + "\n")
        self.process.stdin.flush()
        result = self.process.stdout.readline().strip()
        if not result:
            raise RuntimeError("Native simulator exited")
        return result

    def call(self, message):
        with self.lock:
            self.line("T " + str(int((time.monotonic() - self.started) * 1000)))
            return self.line(message)

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=5)


def main():
    logging.disable(logging.CRITICAL)
    count = 0

    def check(condition, name):
        nonlocal count
        assert condition, name
        count += 1
        print("PASS", name, flush=True)

    native = Native()
    clients = []
    slots, slots_lock = set(), threading.Lock()

    class TcpHandler(socketserver.BaseRequestHandler):
        def handle(self):
            with slots_lock:
                free = set(range(2, 5)) - slots
                if not free:
                    return
                peer = min(free)
                slots.add(peer)

            def exact(n):
                data = b""
                while len(data) < n:
                    part = self.request.recv(n - len(data))
                    if not part:
                        raise EOFError()
                    data += part
                return data

            try:
                while True:
                    header = exact(7)
                    tid, protocol, length, unit = struct.unpack(">HHHB", header)
                    assert protocol == 0 and unit == 1 and 2 <= length <= 254
                    q = exact(length - 1)
                    r = bytes.fromhex(native.call(f"P {peer} " + q.hex()))
                    frame = struct.pack(">HHHB", tid, 0, len(r) + 1, unit) + r
                    # Exercise partial network responses, not just one recv.
                    self.request.sendall(frame[:4])
                    self.request.sendall(frame[4:])
            except (EOFError, ConnectionError):
                pass
            finally:
                native.call(f"D {peer}")
                with slots_lock:
                    slots.remove(peer)

    class Server(socketserver.ThreadingTCPServer):
        daemon_threads = True

    tcp = Server(("127.0.0.1", 0), TcpHandler)
    tcp_thread = threading.Thread(target=tcp.serve_forever, daemon=True)
    tcp_thread.start()
    http = None
    try:
        tcp_client = Modbus(host="127.0.0.1", port=tcp.server_address[1])
        clients.append(tcp_client)
        check(tcp_client.status()["firmware"] == 1122, "TCP reads current firmware")
        check(int(native.call("N")) == 0, "Connecting and reading sends no machine command")
        check(tcp_client.claim()["accepted"], "TCP explicit claim")
        check(
            tcp_client.call("maintenance", enabled=1)["accepted"],
            "Named SDK maintenance through Modbus",
        )
        check(
            tcp_client.call(
                "antenna.power.set", ant1_dbm=20, ant2_dbm=21, ant3_dbm=22, ant4_dbm=23, ram_only=1
            )["accepted"],
            "Four antenna powers",
        )
        ack = tcp_client.call("antenna.power.get")
        check(ack["accepted"], "Reader request accepted")
        check(
            tcp_client.status()["reader_reply"] == [0x94, 0, 20, 21, 22, 23],
            "Reader reply is separate from acceptance",
        )
        tcp_client.release()

        with TunnelDevice.tcp("127.0.0.1", port=tcp.server_address[1]) as public:
            powers = read_power(public)
            check(
                list(powers.values()) == [20, 21, 22, 23],
                "Public TCP API + complete reader example",
            )
            check(not public.has_control(), "Example released TCP ownership")
            console = Console(public)
            try:
                console.run("claim")
                console.run("maintenance", {"enabled": 1})
                result = console.run(
                    "antenna.power.set",
                    dict(
                        ant1_dbm=20,
                        ant2_dbm=21,
                        ant3_dbm=22,
                        ant4_dbm=23,
                        ram_only=1,
                    ),
                )
                check(result["verified"], "TCP console Set/reply/Get readback")
            finally:
                console.close()

        class SerialIO:
            def __init__(self, *args, **kwargs):
                self.data = b""

            def reset_input_buffer(self):
                self.data = b""

            def write(self, frame):
                assert crc16(frame) == 0
                pdu = bytes.fromhex(native.call("P 1 " + frame[1:-2].hex()))
                response = frame[:1] + pdu
                self.data = response + struct.pack("<H", crc16(response))
                return len(frame)

            def read(self, n):
                data, self.data = self.data[: min(n, 3)], self.data[min(n, 3) :]
                return data

            def flush(self):
                pass

            def close(self):
                pass

        with patch("serial.Serial", SerialIO):
            rtu = Modbus(serial_port="SIMULATED")
            clients.append(rtu)
            check(rtu.status()["firmware"] == 1122, "RTU framing/CRC and fragmented serial reads")
            rtu.claim()
            try:
                tcp_client.claim()
            except BusyError:
                check(True, "RTU excludes TCP controller")
            else:
                raise AssertionError("TCP stole RTU")
            check(rtu.execute(8, [3])["accepted"], "RTU FC16 mailbox / FC03 result")
            rtu.release()

            with TunnelDevice.rtu("SIMULATED") as public:
                check(
                    list(read_power(public).values()) == [20, 21, 22, 23],
                    "Public RTU API + complete reader example",
                )
                check(not public.has_control(), "Example released RTU ownership")
                console = Console(public)
                try:
                    console.run("claim")
                    console.run("maintenance", {"enabled": 1})
                    check(
                        list(console.run("antenna.power.get")["decoded"].values())
                        == [20, 21, 22, 23],
                        "RTU console reader completion",
                    )
                finally:
                    console.close()

        tcp_client.claim()
        time.sleep(3.1)
        check(tcp_client.status()["owner"] == 0, "No heartbeat: lease expires without heartbeat")
        print(
            f"PASS {count} integration assertions; native core + real TCP; RTU serial simulated",
            flush=True,
        )
    finally:
        for c in clients:
            with contextlib.suppress(Exception):
                c.close()
        tcp.shutdown()
        tcp.server_close()
        native.close()


if __name__ == "__main__":
    main()
