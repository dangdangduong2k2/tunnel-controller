"""HTTPS download-only test server. No upload/admin endpoint; serves .bin only."""

import argparse
from functools import partial
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
from urllib.parse import urlsplit, unquote


class Handler(BaseHTTPRequestHandler):
    def __init__(self, *args, directory, **kwargs):
        self.directory = Path(directory).resolve()
        super().__init__(*args, **kwargs)

    def do_GET(self):
        path = unquote(urlsplit(self.path).path)
        name = path.removeprefix("/firmware/")
        if (
            not path.startswith("/firmware/")
            or "/" in name
            or "\\" in name
            or not name.endswith(".bin")
        ):
            self.send_error(404)
            return
        file = (self.directory / name).resolve()
        if not file.is_relative_to(self.directory) or not file.is_file():
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(file.stat().st_size))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            with file.open("rb") as stream:
                while chunk := stream.read(4096):
                    self.wfile.write(chunk)
        except (BrokenPipeError, ConnectionResetError):
            pass


def server(directory, cert, key, host="127.0.0.1", port=8443):
    service = ThreadingHTTPServer((host, port), partial(Handler, directory=directory))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)
    service.socket = context.wrap_socket(service.socket, server_side=True)
    return service


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--directory", required=True)
    p.add_argument("--cert", required=True)
    p.add_argument("--key", required=True)
    p.add_argument("--bind", default="127.0.0.1", help="Use 0.0.0.0 deliberately for LAN testing")
    p.add_argument("--port", type=int, default=8443)
    a = p.parse_args()
    server(a.directory, a.cert, a.key, a.bind, a.port).serve_forever()
