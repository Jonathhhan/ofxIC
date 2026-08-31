#!/usr/bin/env python3
"""Deterministic OpenAI-compatible image endpoint for GUI smoke tests."""

import argparse
import base64
import json
import struct
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def png_fixture(width=8, height=8):
    rows = []
    for y in range(height):
        row = bytearray([0])
        for x in range(width):
            row.extend((255 if x < width // 2 else 30,
                        210 if y < height // 2 else 60,
                        40 if (x + y) % 2 == 0 else 220,
                        255))
        rows.append(bytes(row))

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) +
            chunk(b"IEND", b""))


IMAGE_BASE64 = base64.b64encode(png_fixture()).decode("ascii")


class Handler(BaseHTTPRequestHandler):
    def send_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            self.send_json(200, {"status": "ok"})
        else:
            self.send_json(404, {"error": {"message": "not found"}})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        try:
            request = json.loads(self.rfile.read(length))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.send_json(400, {"error": {"message": "invalid JSON"}})
            return
        if self.path != "/v1/images/generations":
            self.send_json(404, {"error": {"message": "not found"}})
            return
        if request.get("prompt") != "deterministic image fixture":
            self.send_json(400, {"error": {"message": "unexpected fixture prompt"}})
            return
        if self.server.request_marker:
            Path(self.server.request_marker).write_text("request received\n", encoding="utf-8")
        if self.server.delay_ms > 0:
            time.sleep(self.server.delay_ms / 1000.0)
        self.send_json(200, {
            "created": 1,
            "output_format": "png",
            "data": [{"b64_json": IMAGE_BASE64}],
        })

    def log_message(self, format, *args):
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18087)
    parser.add_argument("--delay-ms", type=int, default=0)
    parser.add_argument("--request-marker")
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.delay_ms = max(0, args.delay_ms)
    server.request_marker = args.request_marker
    server.serve_forever()


if __name__ == "__main__":
    main()
