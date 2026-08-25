from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import io
import json
import urllib.parse
import wave


def fixture_wav():
    output = io.BytesIO()
    with wave.open(output, "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(48000)
        audio.writeframes(b"\x00\x00" * 4800)
    return output.getvalue()


class Handler(BaseHTTPRequestHandler):
    def send_payload(self, status, content_type, payload):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def send_json(self, status, value):
        self.send_payload(status, "application/json", json.dumps(value).encode())

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        try:
            request = json.loads(body)
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.send_json(400, {"error": "expected JSON request"})
            return
        if self.path == "/lm":
            valid = (
                request.get("caption") == "deterministic timestamp music"
                and request.get("duration") == 1
                and request.get("output_format") == "wav16"
                and self.headers.get("Authorization") is None
            )
            self.send_json(200 if valid else 400,
                           {"id": "lm_fixture_1"} if valid else
                           {"error": "fixture rejected language-model request"})
            return
        if self.path == "/synth":
            valid = (
                request.get("caption") == "enriched deterministic music"
                and request.get("output_format") == "wav16"
                and self.headers.get("Authorization") is None
            )
            self.send_json(202 if valid else 400,
                           {"id": "synth_fixture_1"} if valid else
                           {"error": "fixture rejected synthesis request"})
            return
        self.send_error(404)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != "/job":
            self.send_error(404)
            return
        query = urllib.parse.parse_qs(parsed.query)
        job_id = query.get("id", [""])[0]
        wants_result = query.get("result", [""])[0] == "1"
        if job_id not in ("lm_fixture_1", "synth_fixture_1"):
            self.send_json(404, {"error": "unknown fixture job"})
        elif not wants_result:
            self.send_json(200, {"status": "done"})
        elif job_id == "lm_fixture_1":
            self.send_json(200, {"caption": "enriched deterministic music"})
        else:
            self.send_payload(200, "audio/wav", fixture_wav())

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18086)
    args = parser.parse_args()
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
