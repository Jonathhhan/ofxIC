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
        if self.path == "/release_task":
            if getattr(self.server, "readiness_checks", 0) < getattr(self.server, "readiness_delay_checks", 0):
                self.send_json(503, {"error": "fixture model is still loading"})
                return
            valid = (
                request.get("prompt") == "deterministic timestamp music"
                and request.get("audio_duration") == 10
                and request.get("audio_format") == "wav"
                and request.get("task_type") == "text2music"
                and self.headers.get("Authorization") is None
            )
            self.send_json(200 if valid else 400,
                           {"data": {"task_id": "music-fixture-1",
                                     "status": "queued"},
                            "code": 200, "error": None} if valid else
                           {"code": 400,
                            "error": "fixture rejected release_task request"})
            return
        if self.path == "/query_result":
            valid = (
                request.get("task_id_list") == ["music-fixture-1"]
                and self.headers.get("Authorization") is None
            )
            self.send_json(200 if valid else 400,
                           {"data": [{"task_id": "music-fixture-1",
                                      "status": 1,
                                      "result": "[{\"file\":\"/v1/audio?path=%2Ftmp%2Fmusic.wav\"}]"}],
                            "code": 200, "error": None} if valid else
                           {"code": 400,
                            "error": "fixture rejected query_result request"})
            return
        self.send_error(404)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/health":
            checks = getattr(self.server, "readiness_checks", 0)
            ready = checks >= getattr(self.server, "readiness_delay_checks", 0)
            self.server.readiness_checks = checks + 1
            self.send_json(200, {"code": 200, "data": {
                "status": "ok", "service": "ACE-Step API",
                "models_initialized": ready}})
            return
        if parsed.path != "/v1/audio":
            self.send_error(404)
            return
        query = urllib.parse.parse_qs(parsed.query)
        if query.get("path", [""])[0] != "/tmp/music.wav" or \
                self.headers.get("Authorization") is not None:
            self.send_json(404, {"error": "unknown fixture audio"})
            return
        self.send_payload(200, "audio/wav", fixture_wav())

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18086)
    parser.add_argument("--readiness-delay-checks", type=int, default=0)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.readiness_delay_checks = max(0, args.readiness_delay_checks)
    server.serve_forever()
