import datetime as dt
import importlib.util
import io
import pathlib
import tempfile
import unittest
import unittest.mock
import urllib.error


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "web_snapshot.py"
SPEC = importlib.util.spec_from_file_location("ofxic_web_snapshot", SCRIPT)
web_snapshot = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(web_snapshot)


def public_resolver(host, port, type):
    return [(2, type, 6, "", ("93.184.216.34", port))]


class WebSnapshotTests(unittest.TestCase):
    def test_url_validation_accepts_public_http_and_removes_fragment(self):
        validated = web_snapshot.validate_public_url(
            "HTTPS://example.com/article?q=1#section", resolver=public_resolver
        )
        self.assertEqual(validated, "https://example.com/article?q=1")

    def test_url_validation_rejects_non_http_credentials_and_private_addresses(self):
        with self.assertRaises(web_snapshot.SnapshotError):
            web_snapshot.validate_public_url("file:///etc/passwd", resolver=public_resolver)
        with self.assertRaises(web_snapshot.SnapshotError):
            web_snapshot.validate_public_url("https://user:secret@example.com/", resolver=public_resolver)

        def private_resolver(host, port, type):
            return [(2, type, 6, "", ("127.0.0.1", port))]

        with self.assertRaises(web_snapshot.SnapshotError):
            web_snapshot.validate_public_url("http://localhost/admin", resolver=private_resolver)

    def test_html_extraction_keeps_content_and_removes_active_or_navigation_text(self):
        html = b"""
            <html><head><title>Grounded &amp; Small</title><script>ignore()</script></head>
            <body><nav>menu secret</nav><main><h1>Process boundary</h1>
            <p>Inference stays outside &amp; independently updated.</p>
            <form>malicious instructions</form></main></body></html>
        """
        title, text = web_snapshot.extract_readable_text(html, "text/html", "utf-8")
        self.assertEqual(title, "Grounded & Small")
        self.assertIn("Process boundary", text)
        self.assertIn("outside & independently", text)
        self.assertNotIn("ignore", text)
        self.assertNotIn("menu secret", text)
        self.assertNotIn("malicious instructions", text)

    def test_response_reader_is_bounded(self):
        self.assertEqual(web_snapshot._read_limited(io.BytesIO(b"1234"), 4), b"1234")
        with self.assertRaises(web_snapshot.SnapshotError):
            web_snapshot._read_limited(io.BytesIO(b"12345"), 4)

    def test_redirect_destination_is_revalidated_before_the_next_request(self):
        class RedirectOpener:
            def open(self, request, timeout):
                raise urllib.error.HTTPError(
                    request.full_url,
                    302,
                    "Found",
                    {"Location": "http://127.0.0.1/private"},
                    None,
                )

        visited = []

        def validate(url):
            visited.append(url)
            if "127.0.0.1" in url:
                raise web_snapshot.SnapshotError("private redirect rejected")
            return url

        with unittest.mock.patch.object(web_snapshot.urllib.request, "build_opener", return_value=RedirectOpener()), \
                unittest.mock.patch.object(web_snapshot, "validate_public_url", side_effect=validate):
            with self.assertRaisesRegex(web_snapshot.SnapshotError, "private redirect"):
                web_snapshot.fetch_one_url("https://example.com/")
        self.assertEqual(visited, ["https://example.com/", "http://127.0.0.1/private"])

    def test_snapshot_records_stable_provenance_and_writes_only_document_extensions(self):
        text = "A grounded statement."
        snapshot = web_snapshot.format_snapshot(
            "https://example.com/article",
            "Example",
            text,
            dt.datetime(2026, 8, 25, 10, 30, tzinfo=dt.timezone.utc),
        )
        self.assertIn("Source URL: https://example.com/article", snapshot)
        self.assertIn("Retrieved UTC: 2026-08-25T10:30:00Z", snapshot)
        self.assertIn("Content SHA-256:", snapshot)
        self.assertTrue(snapshot.endswith(text + "\n"))

        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "snapshot.txt"
            web_snapshot.write_snapshot(str(output), snapshot)
            self.assertEqual(output.read_text(encoding="utf-8"), snapshot)
            with self.assertRaises(web_snapshot.SnapshotError):
                web_snapshot.write_snapshot(str(output), snapshot)
            with self.assertRaises(web_snapshot.SnapshotError):
                web_snapshot.write_snapshot(str(pathlib.Path(directory) / "snapshot.html"), snapshot)


if __name__ == "__main__":
    unittest.main()
