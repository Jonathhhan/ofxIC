import importlib.util
import pathlib
import json
import sys
import threading
import unittest
import urllib.request


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "sam-bridge-server.py"
SPEC = importlib.util.spec_from_file_location("ofxic_sam_bridge", SCRIPT)
sam_bridge = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(sam_bridge)


class SamBridgeTests(unittest.TestCase):
    def test_adapter_command_forwards_backend_and_all_points(self):
        command = sam_bridge.adapter_command(
            pathlib.Path("sam-runner"),
            pathlib.Path("model.ggml"),
            pathlib.Path("input.ppm"),
            pathlib.Path("mask.pgm"),
            [("0.5", "0.25", "positive"), ("0.1", "0.2", "negative")],
            "cuda",
        )
        self.assertEqual(command[:8], [
            "sam-runner", "--model", "model.ggml", "--image", "input.ppm",
            "--output", "mask.pgm", "--backend",
        ])
        self.assertEqual(command[8], "cuda")
        self.assertEqual(command.count("--point-x"), 2)
        self.assertEqual(command[-6:], [
            "--point-x", "0.1", "--point-y", "0.2", "--point-label", "negative",
        ])

    def test_adapter_command_keeps_backend_optional_for_generic_runners(self):
        command = sam_bridge.adapter_command(
            "sam-runner", "model.ggml", "input.ppm", "mask.pgm", [], None)
        self.assertNotIn("--backend", command)

    def test_python_adapter_uses_bridge_environment_interpreter(self):
        command = sam_bridge.adapter_command(
            pathlib.Path("sam-python-runner.py"), "model.pth", "input.ppm",
            "mask.pgm", [("0.5", "0.5", "positive")], "cuda")
        self.assertEqual(command[:2], [sys.executable, "sam-python-runner.py"])

    def test_adapter_command_rejects_invalid_points(self):
        with self.assertRaises(ValueError):
            sam_bridge.adapter_command(
                "sam-runner", "model.ggml", "input.ppm", "mask.pgm",
                [("1.5", "0.2", "positive")], "cuda")

    def test_health_route_reports_version_mode_and_backend(self):
        sam_bridge.Handler.fixture_mask = False
        sam_bridge.Handler.backend = "cuda"
        server = sam_bridge.ThreadingHTTPServer(("127.0.0.1", 0), sam_bridge.Handler)
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            with urllib.request.urlopen(
                    f"http://127.0.0.1:{server.server_port}/health", timeout=2) as response:
                health = json.loads(response.read())
                self.assertEqual(response.headers["X-ofxIC-SAM-Bridge-Version"], "1")
            self.assertEqual(health, {
                "status": "ok", "version": "1", "mode": "runner", "backend": "cuda"
            })
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()
