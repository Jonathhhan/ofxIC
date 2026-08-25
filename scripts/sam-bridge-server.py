from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import json
import math
import subprocess
import tempfile
import threading


BRIDGE_VERSION = "1"
MAXIMUM_REQUEST_BYTES = 64 * 1024 * 1024
MAXIMUM_IMAGE_BYTES = 64 * 1024 * 1024
MAXIMUM_MASK_BYTES = 64 * 1024 * 1024
MAXIMUM_POINTS = 64


def validate_portable_map(data, magic, maximum_bytes):
    if len(data) > maximum_bytes:
        raise ValueError(f"{magic.decode()} file exceeds the size limit")
    index = 0
    tokens = []
    while len(tokens) < 4:
        while index < len(data) and data[index] in b" \t\r\n":
            index += 1
        if index < len(data) and data[index] == ord("#"):
            while index < len(data) and data[index] not in b"\r\n":
                index += 1
            continue
        start = index
        while index < len(data) and data[index] not in b" \t\r\n":
            index += 1
        if start == index:
            raise ValueError("portable map header is incomplete")
        tokens.append(data[start:index])
    if tokens[0] != magic:
        raise ValueError(f"expected {magic.decode()} data")
    width, height, maximum = map(int, tokens[1:])
    if width <= 0 or height <= 0 or width > 8192 or height > 8192 or maximum != 255:
        raise ValueError("portable map dimensions or maximum value are invalid")
    if index >= len(data) or data[index] not in b" \t\r\n":
        raise ValueError("portable map header has no pixel separator")
    index += 2 if data[index:index + 2] == b"\r\n" else 1
    if len(data) - index != width * height * (3 if magic == b"P6" else 1):
        raise ValueError("portable map pixel payload has the wrong size")


def validate_pgm(data):
    if data.startswith(b"P5"):
        validate_portable_map(data, b"P5", MAXIMUM_MASK_BYTES)
        return
    if len(data) > MAXIMUM_MASK_BYTES or not data.startswith(b"P2"):
        raise ValueError("SAM adapter returned no supported PGM mask")
    without_comments = b"\n".join(line.split(b"#", 1)[0] for line in data.splitlines())
    tokens = without_comments.split()
    if len(tokens) < 4 or tokens[0] != b"P2":
        raise ValueError("ASCII PGM header is incomplete")
    width, height, maximum = map(int, tokens[1:4])
    pixels = tokens[4:]
    if width <= 0 or height <= 0 or width > 8192 or height > 8192 or maximum != 255:
        raise ValueError("ASCII PGM dimensions or maximum value are invalid")
    if len(pixels) != width * height or any(not 0 <= int(value) <= 255 for value in pixels):
        raise ValueError("ASCII PGM pixel payload is invalid")


def adapter_command(adapter, model, image_path, mask_path, points, backend=None):
    command = [str(adapter), "--model", str(model),
               "--image", str(image_path), "--output", str(mask_path)]
    if backend:
        command += ["--backend", backend]
    for point in points:
        if len(point) != 3:
            raise ValueError("point must contain x,y,label")
        x, y, label = point
        float_x, float_y = float(x), float(y)
        if not math.isfinite(float_x) or not math.isfinite(float_y) or \
                not 0.0 <= float_x <= 1.0 or not 0.0 <= float_y <= 1.0:
            raise ValueError("point coordinates must be normalized")
        if label not in ("positive", "negative"):
            raise ValueError("point label is invalid")
        command += ["--point-x", x, "--point-y", y, "--point-label", label]
    return command


def parse_multipart(content_type, body):
    marker = "boundary="
    if marker not in content_type:
        raise ValueError("multipart boundary is missing")
    boundary = content_type.split(marker, 1)[1].strip().strip('"').encode()
    fields = []
    for part in body.split(b"--" + boundary)[1:-1]:
        part = part.strip(b"\r\n")
        headers, separator, value = part.partition(b"\r\n\r\n")
        if not separator:
            continue
        disposition = next(
            (line.decode("utf-8", "replace") for line in headers.split(b"\r\n")
             if line.lower().startswith(b"content-disposition:")), "")
        name = ""
        for item in disposition.split(";"):
            item = item.strip()
            if item.startswith("name="):
                name = item.split("=", 1)[1].strip('"')
        fields.append((name, value.rstrip(b"\r\n")))
    return fields


class Handler(BaseHTTPRequestHandler):
    adapter = None
    model = None
    backend = None
    fixture_mask = False
    runner_timeout = 300
    runner_lock = threading.Lock()

    def send_problem(self, status, code, message):
        body = f"{code}: {message}".encode("utf-8", "replace")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-ofxIC-SAM-Bridge-Version", BRIDGE_VERSION)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path != "/health":
            self.send_problem(404, "route_not_found", "use /health or /v1/segmentations")
            return
        body = json.dumps({
            "status": "ok",
            "version": BRIDGE_VERSION,
            "mode": "fixture" if self.fixture_mask else "runner",
            "backend": self.backend or "runner-default",
        }).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-ofxIC-SAM-Bridge-Version", BRIDGE_VERSION)
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/v1/segmentations":
            self.send_problem(404, "route_not_found", "use /v1/segmentations")
            return
        if self.headers.get("X-ofxIC-SAM-Bridge-Version") != BRIDGE_VERSION:
            self.send_problem(426, "bridge_version_mismatch", "SAM bridge v1 is required")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0:
                raise ValueError("request body is empty")
            if length > MAXIMUM_REQUEST_BYTES:
                self.send_problem(413, "request_too_large", "request exceeds 64 MiB")
                return
            self.connection.settimeout(30)
            fields = parse_multipart(
                self.headers.get("Content-Type", ""), self.rfile.read(length))
            image = next(value for name, value in fields if name == "image")
            points = [value.decode("ascii").split(",") for name, value in fields
                      if name == "point"]
            if not points:
                raise ValueError("PPM image and at least one point are required")
            if len(points) > MAXIMUM_POINTS:
                raise ValueError("at most 64 points are accepted")
            validate_portable_map(image, b"P6", MAXIMUM_IMAGE_BYTES)
            if self.fixture_mask:
                mask = b"P5\n2 2\n255\n\x00\xff\xff\x00"
            else:
                if not self.runner_lock.acquire(blocking=False):
                    self.send_problem(429, "runner_busy", "another segmentation is running")
                    return
                try:
                    mask = self.run_adapter(image, points)
                finally:
                    self.runner_lock.release()
            self.send_response(200)
            self.send_header("Content-Type", "image/x-portable-graymap")
            self.send_header("Content-Length", str(len(mask)))
            self.send_header("X-ofxIC-SAM-Bridge-Version", BRIDGE_VERSION)
            self.end_headers()
            self.wfile.write(mask)
        except (ValueError, StopIteration) as error:
            self.send_problem(400, "invalid_request", str(error))
        except subprocess.TimeoutExpired:
            self.send_problem(504, "runner_timeout", "SAM adapter exceeded its timeout")
        except subprocess.CalledProcessError as error:
            self.send_problem(502, "runner_failed", f"SAM adapter exited with {error.returncode}")
        except (OSError, TimeoutError) as error:
            self.send_problem(502, "runner_io_failed", str(error))

    def run_adapter(self, image, points):
        with tempfile.TemporaryDirectory(prefix="ofxIC-sam-bridge-") as directory:
            image_path = Path(directory) / "input.ppm"
            mask_path = Path(directory) / "mask.pgm"
            image_path.write_bytes(image)
            command = adapter_command(
                self.adapter, self.model, image_path, mask_path, points, self.backend)
            subprocess.run(command, check=True, timeout=self.runner_timeout)
            mask = mask_path.read_bytes()
            validate_pgm(mask)
            return mask

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ofxIC SAM bridge v1")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18085)
    parser.add_argument("--adapter", type=Path)
    parser.add_argument("--model", type=Path)
    parser.add_argument("--backend", choices=("cpu", "cuda"))
    parser.add_argument("--fixture-mask", action="store_true")
    parser.add_argument("--runner-timeout", type=int, default=300)
    args = parser.parse_args()
    if not args.fixture_mask:
        if not args.adapter or not args.adapter.is_file():
            parser.error("--adapter must name an existing sam-runner executable")
        if not args.model or not args.model.is_file():
            parser.error("--model must name an existing SAM model")
    Handler.adapter = args.adapter
    Handler.model = args.model
    Handler.backend = args.backend
    Handler.fixture_mask = args.fixture_mask
    if args.runner_timeout < 1 or args.runner_timeout > 3600:
        parser.error("--runner-timeout must be between 1 and 3600 seconds")
    Handler.runner_timeout = args.runner_timeout
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()
