from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json
import time


class Handler(BaseHTTPRequestHandler):
    delay_seconds = 30.0

    def do_GET(self):
        if self.path != "/v1/models":
            self.send_error(404)
            return
        time.sleep(self.delay_seconds)
        body = json.dumps({"data": [{"id": "slow-fixture"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18081)
    parser.add_argument("--delay", type=float, default=30.0)
    args = parser.parse_args()
    Handler.delay_seconds = args.delay
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
