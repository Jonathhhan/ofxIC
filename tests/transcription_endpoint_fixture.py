from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json


class Handler(BaseHTTPRequestHandler):
    expected_marker = b"OFXIC_AUDIO_FIXTURE"

    def do_POST(self):
        if self.path not in ("/v1/audio/transcriptions", "/inference"):
            self.send_error(404)
            return
        content_type = self.headers.get("Content-Type", "")
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        valid = (
            content_type.startswith("multipart/form-data; boundary=")
            and b'name="file"; filename="fixture.wav"' in body
            and self.expected_marker in body
        )
        if self.path == "/v1/audio/transcriptions":
            valid = valid and b'name="model"' in body and b"whisper-1" in body
        else:
            valid = valid and b'name="model"' not in body
        response = {"text": "deterministic GUI transcript"} if valid else {
            "error": "fixture rejected multipart request"
        }
        payload = json.dumps(response).encode()
        self.send_response(200 if valid else 400)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18082)
    args = parser.parse_args()
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
