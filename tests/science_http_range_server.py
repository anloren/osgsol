#!/usr/bin/env python3
"""Serve one fixture with strict, instrumented single-range HTTP semantics."""

import argparse
import http.server
import json
import os
import re
import signal
import threading
from pathlib import Path
from urllib.parse import unquote, urlsplit


RANGE_PATTERN = re.compile(r"^bytes=(\d+)-(\d*)$")


class RangeState:
    def __init__(self, source_path, log_path, budget):
        self.source_path = source_path
        self.source_size = source_path.stat().st_size
        self.log_path = log_path
        self.budget = budget
        self.bytes_sent = 0
        self.reserved_bytes = 0
        self.violations = []
        self.lock = threading.Lock()

    def record(self, method, uri, range_header, response_code, bytes_sent,
               violation=None, reserved=False):
        with self.lock:
            if reserved:
                self.reserved_bytes -= bytes_sent
            else:
                self.bytes_sent += bytes_sent
            if violation:
                self.violations.append(violation)
            entry = {
                "method": method,
                "uri": uri,
                "range": range_header,
                "response_code": response_code,
                "bytes_sent": bytes_sent,
                "total_bytes_sent": self.bytes_sent,
                "source_size": self.source_size,
                "violation": violation,
            }
            with self.log_path.open("a", encoding="utf-8") as stream:
                json.dump(entry, stream, sort_keys=True)
                stream.write("\n")
                stream.flush()

    def reserve(self, amount):
        with self.lock:
            if self.bytes_sent + self.reserved_bytes + amount > self.budget:
                return False
            self.bytes_sent += amount
            self.reserved_bytes += amount
            return True


class StrictRangeHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    state = None

    def log_message(self, unused_format, *unused_arguments):
        return

    def fixture_request(self):
        path = unquote(urlsplit(self.path).path)
        return path == "/" + self.state.source_path.name

    def send_empty(self, response_code, violation=None):
        self.send_response(response_code)
        self.send_header("Content-Length", "0")
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        self.state.record(
            self.command, self.path, self.headers.get("Range"), response_code, 0,
            violation)

    def do_HEAD(self):
        if not self.fixture_request():
            self.send_empty(404, "unexpected_uri")
            return
        self.send_response(200)
        self.send_header("Content-Length", str(self.state.source_size))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        self.state.record("HEAD", self.path, self.headers.get("Range"), 200, 0)

    def do_GET(self):
        if not self.fixture_request():
            self.send_empty(404, "unexpected_uri")
            return

        range_header = self.headers.get("Range")
        if range_header is None:
            self.send_empty(412, "missing_range")
            return
        if "," in range_header:
            self.send_empty(400, "comma_multirange")
            return
        match = RANGE_PATTERN.fullmatch(range_header)
        if not match:
            self.send_empty(400, "invalid_range")
            return

        start = int(match.group(1))
        requested_end = int(match.group(2)) if match.group(2) else self.state.source_size - 1
        if start >= self.state.source_size or requested_end < start:
            self.send_response(416)
            self.send_header("Content-Range", "bytes */{}".format(self.state.source_size))
            self.send_header("Content-Length", "0")
            self.end_headers()
            self.state.record("GET", self.path, range_header, 416, 0, "invalid_range")
            return
        end = min(requested_end, self.state.source_size - 1)
        length = end - start + 1
        if not self.state.reserve(length):
            self.send_empty(509, "budget_exceeded")
            return

        self.send_response(206)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Range", "bytes {}-{}/{}".format(
            start, end, self.state.source_size))
        self.send_header("Content-Length", str(length))
        self.send_header("Content-Type", "image/tiff")
        self.end_headers()
        with self.state.source_path.open("rb") as stream:
            stream.seek(start)
            payload = stream.read(length)
        self.wfile.write(payload)
        self.state.record("GET", self.path, range_header, 206, len(payload), reserved=True)


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", required=True)
    parser.add_argument("--ready-file", required=True)
    parser.add_argument("--log-file", required=True)
    parser.add_argument("--budget-bytes", required=True, type=int)
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    source_path = Path(arguments.file).resolve()
    ready_path = Path(arguments.ready_file).resolve()
    log_path = Path(arguments.log_file).resolve()
    if not source_path.is_file():
        raise SystemExit("fixture is not a regular file")
    if arguments.budget_bytes <= 0:
        raise SystemExit("budget must be positive")
    ready_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text("", encoding="utf-8")

    state = RangeState(source_path, log_path, arguments.budget_bytes)
    StrictRangeHandler.state = state
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), StrictRangeHandler)
    server.daemon_threads = True

    def stop_server(unused_signal, unused_frame):
        threading.Thread(target=server.shutdown, daemon=True).start()

    signal.signal(signal.SIGTERM, stop_server)
    temporary_ready = ready_path.with_name(ready_path.name + ".tmp")
    temporary_ready.write_text(str(server.server_address[1]) + "\n", encoding="ascii")
    os.replace(temporary_ready, ready_path)
    try:
        server.serve_forever(poll_interval=0.05)
    finally:
        server.server_close()
    if state.violations:
        raise SystemExit("range contract violations: " + ", ".join(state.violations))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
