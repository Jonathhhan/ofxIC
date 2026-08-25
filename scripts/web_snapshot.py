#!/usr/bin/env python3
"""Fetch one public web page and save bounded, provenance-bearing plain text.

This is an explicit ingestion utility, not a crawler or a model tool. It does
not follow page links, execute JavaScript, send cookies, or access private
network addresses.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import html.parser
import ipaddress
import os
import pathlib
import re
import socket
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request


MAX_URL_CHARACTERS = 8192
MAX_RESPONSE_BYTES = 4 * 1024 * 1024
MAX_REDIRECTS = 5
DEFAULT_TIMEOUT_SECONDS = 15.0
ALLOWED_CONTENT_TYPES = {"text/html", "application/xhtml+xml", "text/plain"}
REDIRECT_STATUSES = {301, 302, 303, 307, 308}


class SnapshotError(RuntimeError):
    pass


def _public_ip(address: str) -> bool:
    parsed = ipaddress.ip_address(address.split("%", 1)[0])
    if isinstance(parsed, ipaddress.IPv6Address) and parsed.ipv4_mapped:
        parsed = parsed.ipv4_mapped
    return parsed.is_global


def validate_public_url(url: str, resolver=None) -> str:
    if not url or len(url) > MAX_URL_CHARACTERS:
        raise SnapshotError("URL is empty or exceeds the URL length limit")
    if any(ord(character) < 0x20 or ord(character) == 0x7F for character in url):
        raise SnapshotError("URL contains control characters")

    parsed = urllib.parse.urlsplit(url)
    scheme = parsed.scheme.lower()
    if scheme not in {"http", "https"}:
        raise SnapshotError("only http and https URLs are supported")
    if parsed.username is not None or parsed.password is not None:
        raise SnapshotError("credentials in URLs are not supported")
    if not parsed.hostname:
        raise SnapshotError("URL has no host")
    try:
        port = parsed.port or (443 if scheme == "https" else 80)
    except ValueError as error:
        raise SnapshotError("URL has an invalid port") from error

    lookup = resolver or socket.getaddrinfo
    try:
        addresses = lookup(parsed.hostname, port, type=socket.SOCK_STREAM)
    except OSError as error:
        raise SnapshotError(f"could not resolve URL host: {error}") from error
    if not addresses:
        raise SnapshotError("URL host resolved to no addresses")
    try:
        if any(not _public_ip(item[4][0]) for item in addresses):
            raise SnapshotError("URL host resolves to a non-public address")
    except ValueError as error:
        raise SnapshotError("URL host resolved to an invalid address") from error

    return urllib.parse.urlunsplit((scheme, parsed.netloc, parsed.path or "/", parsed.query, ""))


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file_pointer, code, message, headers, new_url):
        return None


def _read_limited(response, maximum_bytes: int = MAX_RESPONSE_BYTES) -> bytes:
    content = bytearray()
    while len(content) <= maximum_bytes:
        block = response.read(min(65536, maximum_bytes + 1 - len(content)))
        if not block:
            return bytes(content)
        content.extend(block)
    raise SnapshotError(f"response exceeds the {maximum_bytes}-byte limit")


def fetch_one_url(
    url: str,
    timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
    maximum_bytes: int = MAX_RESPONSE_BYTES,
) -> tuple[str, str, str, bytes]:
    opener = urllib.request.build_opener(_NoRedirect)
    current = url
    for redirect_count in range(MAX_REDIRECTS + 1):
        current = validate_public_url(current)
        request = urllib.request.Request(
            current,
            headers={
                "User-Agent": "ofxIC-web-snapshot/1",
                "Accept": "text/html,application/xhtml+xml,text/plain;q=0.9",
                "Accept-Encoding": "identity",
            },
            method="GET",
        )
        try:
            response = opener.open(request, timeout=timeout_seconds)
        except urllib.error.HTTPError as error:
            if error.code in REDIRECT_STATUSES:
                if redirect_count >= MAX_REDIRECTS:
                    raise SnapshotError("redirect limit reached") from error
                location = error.headers.get("Location")
                error.close()
                if not location:
                    raise SnapshotError("redirect response has no Location header") from error
                current = urllib.parse.urljoin(current, location)
                continue
            raise SnapshotError(f"server returned HTTP {error.code}") from error
        except urllib.error.URLError as error:
            raise SnapshotError(f"request failed: {error.reason}") from error

        with response:
            final_url = validate_public_url(response.geturl())
            content_type = response.headers.get_content_type().lower()
            if content_type not in ALLOWED_CONTENT_TYPES:
                raise SnapshotError(f"unsupported Content-Type: {content_type}")
            content_encoding = (response.headers.get("Content-Encoding") or "identity").lower()
            if content_encoding not in {"", "identity"}:
                raise SnapshotError(f"unsupported Content-Encoding: {content_encoding}")
            charset = response.headers.get_content_charset() or "utf-8"
            return final_url, content_type, charset, _read_limited(response, maximum_bytes)

    raise SnapshotError("redirect limit reached")


class _ReadableHtml(html.parser.HTMLParser):
    ignored_tags = {"script", "style", "noscript", "template", "svg", "canvas", "form", "nav", "aside"}
    block_tags = {
        "address", "article", "blockquote", "br", "dd", "div", "dl", "dt", "figcaption",
        "figure", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "li", "main", "p",
        "pre", "section", "table", "tbody", "td", "tfoot", "th", "thead", "tr", "ul", "ol",
    }

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.fragments: list[str] = []
        self.title_fragments: list[str] = []
        self.ignored_depth = 0
        self.in_title = False

    def handle_starttag(self, tag: str, attrs) -> None:
        tag = tag.lower()
        if tag in self.ignored_tags:
            self.ignored_depth += 1
            return
        if self.ignored_depth:
            return
        if tag == "title":
            self.in_title = True
        if tag in self.block_tags:
            self.fragments.append("\n")

    def handle_startendtag(self, tag: str, attrs) -> None:
        if not self.ignored_depth and tag.lower() in self.block_tags:
            self.fragments.append("\n")

    def handle_endtag(self, tag: str) -> None:
        tag = tag.lower()
        if tag in self.ignored_tags and self.ignored_depth:
            self.ignored_depth -= 1
            return
        if self.ignored_depth:
            return
        if tag == "title":
            self.in_title = False
        if tag in self.block_tags:
            self.fragments.append("\n")

    def handle_data(self, data: str) -> None:
        if self.ignored_depth:
            return
        if self.in_title:
            self.title_fragments.append(data)
        else:
            self.fragments.append(data)


def _clean_text(value: str) -> str:
    value = value.replace("\r\n", "\n").replace("\r", "\n").replace("\u00a0", " ")
    value = "".join(character for character in value if character in "\n\t" or ord(character) >= 0x20)
    lines = [re.sub(r"[\t ]+", " ", line).strip() for line in value.split("\n")]
    output: list[str] = []
    for line in lines:
        if line:
            output.append(line)
        elif output and output[-1] != "":
            output.append("")
    return "\n".join(output).strip()


def extract_readable_text(body: bytes, content_type: str, charset: str) -> tuple[str, str]:
    try:
        decoded = body.decode(charset, errors="replace")
    except LookupError as error:
        raise SnapshotError(f"unsupported character encoding: {charset}") from error
    if content_type == "text/plain":
        text = _clean_text(decoded)
        title = ""
    else:
        parser = _ReadableHtml()
        parser.feed(decoded)
        parser.close()
        title = _clean_text(" ".join(parser.title_fragments)).replace("\n", " ")
        text = _clean_text("".join(parser.fragments))
    if not text:
        raise SnapshotError("page contains no readable text")
    return title, text


def format_snapshot(final_url: str, title: str, text: str, retrieved_at: dt.datetime | None = None) -> str:
    timestamp = retrieved_at or dt.datetime.now(dt.timezone.utc)
    timestamp = timestamp.astimezone(dt.timezone.utc).replace(microsecond=0)
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    header = [
        f"Source URL: {final_url}",
        f"Retrieved UTC: {timestamp.isoformat().replace('+00:00', 'Z')}",
        f"Content SHA-256: {digest}",
    ]
    if title:
        header.insert(2, f"Title: {title}")
    return "\n".join(header) + "\n\n" + text + "\n"


def write_snapshot(path: str, snapshot: str, force: bool = False) -> pathlib.Path:
    output = pathlib.Path(path).expanduser()
    if output.suffix.lower() not in {".txt", ".md"}:
        raise SnapshotError("output must use a .txt or .md extension")
    if not output.parent.is_dir():
        raise SnapshotError("output directory does not exist")
    if output.exists() and not force:
        raise SnapshotError("output already exists; pass --force to replace it")

    temporary_name = ""
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="\n",
            prefix=f".{output.name}.",
            suffix=".tmp",
            dir=output.parent,
            delete=False,
        ) as temporary:
            temporary_name = temporary.name
            temporary.write(snapshot)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_name, output)
    except OSError as error:
        if temporary_name:
            pathlib.Path(temporary_name).unlink(missing_ok=True)
        raise SnapshotError(f"could not write output: {error}") from error
    return output


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Save one public HTML or text URL as a bounded ofxIC document snapshot."
    )
    parser.add_argument("url", help="one explicit public http(s) URL")
    parser.add_argument("--output", "-o", required=True, help="destination .txt or .md file")
    parser.add_argument("--force", action="store_true", help="replace an existing output file")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS, help="request timeout in seconds")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv if argv is not None else sys.argv[1:])
    if arguments.timeout <= 0 or arguments.timeout > 60:
        print("web snapshot failed: timeout must be greater than 0 and at most 60 seconds", file=sys.stderr)
        return 2
    try:
        final_url, content_type, charset, body = fetch_one_url(arguments.url, arguments.timeout)
        title, text = extract_readable_text(body, content_type, charset)
        output = write_snapshot(arguments.output, format_snapshot(final_url, title, text), arguments.force)
    except SnapshotError as error:
        print(f"web snapshot failed: {error}", file=sys.stderr)
        return 1
    print(f"Saved {len(text.encode('utf-8'))} readable bytes from {final_url} to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
