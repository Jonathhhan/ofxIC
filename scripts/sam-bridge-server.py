from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import subprocess
import tempfile


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
    fixture_mask = False
    maximum_bytes = 256 * 1024 * 1024

    def do_POST(self):
        if self.path != "/v1/segmentations":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > self.maximum_bytes:
            self.send_error(413)
            return
        try:
            fields = parse_multipart(
                self.headers.get("Content-Type", ""), self.rfile.read(length))
            image = next(value for name, value in fields if name == "image")
            points = [value.decode("ascii").split(",") for name, value in fields
                      if name == "point"]
            if not image.startswith(b"P6") or not points:
                raise ValueError("PPM image and at least one point are required")
            if self.fixture_mask:
                mask = b"P5\n2 2\n255\n\x00\xff\xff\x00"
            else:
                mask = self.run_adapter(image, points)
            self.send_response(200)
            self.send_header("Content-Type", "image/x-portable-graymap")
            self.send_header("Content-Length", str(len(mask)))
            self.end_headers()
            self.wfile.write(mask)
        except (ValueError, StopIteration) as error:
            self.send_error(400, str(error))
        except subprocess.CalledProcessError as error:
            self.send_error(502, f"SAM adapter exited with {error.returncode}")
        except (OSError, TimeoutError) as error:
            self.send_error(502, str(error))

    def run_adapter(self, image, points):
        with tempfile.TemporaryDirectory(prefix="ofxIC-sam-bridge-") as directory:
            image_path = Path(directory) / "input.ppm"
            mask_path = Path(directory) / "mask.pgm"
            image_path.write_bytes(image)
            command = [str(self.adapter), "--model", str(self.model),
                       "--image", str(image_path), "--output", str(mask_path)]
            for x, y, label in points:
                float_x, float_y = float(x), float(y)
                if not 0.0 <= float_x <= 1.0 or not 0.0 <= float_y <= 1.0:
                    raise ValueError("point coordinates must be normalized")
                if label not in ("positive", "negative"):
                    raise ValueError("point label is invalid")
                command += ["--point-x", x, "--point-y", y, "--point-label", label]
            subprocess.run(command, check=True, timeout=300)
            mask = mask_path.read_bytes()
            if not mask.startswith((b"P5", b"P2")):
                raise OSError("SAM adapter returned no PGM mask")
            return mask

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ofxIC SAM bridge v1")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18085)
    parser.add_argument("--adapter", type=Path)
    parser.add_argument("--model", type=Path)
    parser.add_argument("--fixture-mask", action="store_true")
    args = parser.parse_args()
    if not args.fixture_mask:
        if not args.adapter or not args.adapter.is_file():
            parser.error("--adapter must name an existing sam-runner executable")
        if not args.model or not args.model.is_file():
            parser.error("--model must name an existing SAM model")
    Handler.adapter = args.adapter
    Handler.model = args.model
    Handler.fixture_mask = args.fixture_mask
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()
