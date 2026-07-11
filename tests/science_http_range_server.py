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
WRITE_CHUNK_BYTES = 64 * 1024


class RangeState:
    def __init__(self, source_path, log_path, budget):
        self.source_path = source_path
        self.source_size = source_path.stat().st_size
        self.log_path = log_path
        self.budget = budget
        self.committed_bytes = 0
        self.reserved_bytes = 0
        self.violations = []
        self.next_request_id = 1
        self.lock = threading.Lock()

    def write_entry_locked(self, entry):
        with self.log_path.open("a", encoding="utf-8") as stream:
            json.dump(entry, stream, sort_keys=True)
            stream.write("\n")
            stream.flush()

    def begin(self, method, uri, range_header):
        with self.lock:
            request_id = self.next_request_id
            self.next_request_id += 1
            self.write_entry_locked({
                "event": "request_start",
                "request_id": request_id,
                "method": method,
                "uri": uri,
                "range": range_header,
                "source_size": self.source_size,
            })
            return request_id

    def complete(self, request_id, method, uri, range_header, response_code,
                 planned_bytes, actual_bytes_sent, reserved_bytes=0,
                 violation=None, partial_write=False):
        with self.lock:
            self.reserved_bytes -= reserved_bytes
            if self.reserved_bytes < 0:
                raise RuntimeError("reserved-byte accounting underflow")
            self.committed_bytes += actual_bytes_sent
            if violation:
                self.violations.append(violation)
            self.write_entry_locked({
                "event": "request_complete",
                "request_id": request_id,
                "method": method,
                "uri": uri,
                "range": range_header,
                "response_code": response_code,
                "planned_bytes": planned_bytes,
                "actual_bytes_sent": actual_bytes_sent,
                "total_committed_bytes": self.committed_bytes,
                "total_reserved_bytes": self.reserved_bytes,
                "source_size": self.source_size,
                "violation": violation,
                "partial_write": partial_write,
            })

    def reserve(self, amount):
        with self.lock:
            if self.committed_bytes + self.reserved_bytes + amount > self.budget:
                return False
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

    def send_empty(self, request_id, response_code, violation=None):
        partial_write = False
        try:
            self.send_response(response_code)
            self.send_header("Content-Length", "0")
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()
            self.wfile.flush()
        except (BrokenPipeError, ConnectionError, OSError) as error:
            partial_write = True
            violation = "response_write_failed:{}".format(type(error).__name__)
        self.state.complete(
            request_id, self.command, self.path, self.headers.get("Range"),
            response_code, 0, 0, violation=violation,
            partial_write=partial_write)

    def do_HEAD(self):
        request_id = self.state.begin("HEAD", self.path, self.headers.get("Range"))
        if not self.fixture_request():
            self.send_empty(request_id, 404, "unexpected_uri")
            return
        partial_write = False
        violation = None
        try:
            self.send_response(200)
            self.send_header("Content-Length", str(self.state.source_size))
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()
            self.wfile.flush()
        except (BrokenPipeError, ConnectionError, OSError) as error:
            partial_write = True
            violation = "response_write_failed:{}".format(type(error).__name__)
        self.state.complete(
            request_id, "HEAD", self.path, self.headers.get("Range"), 200,
            0, 0, violation=violation, partial_write=partial_write)

    def do_GET(self):
        request_id = self.state.begin("GET", self.path, self.headers.get("Range"))
        if not self.fixture_request():
            self.send_empty(request_id, 404, "unexpected_uri")
            return

        range_header = self.headers.get("Range")
        if range_header is None:
            self.send_empty(request_id, 412, "missing_range")
            return
        if "," in range_header:
            self.send_empty(request_id, 400, "comma_multirange")
            return
        match = RANGE_PATTERN.fullmatch(range_header)
        if not match:
            self.send_empty(request_id, 400, "invalid_range")
            return

        start = int(match.group(1))
        requested_end = int(match.group(2)) if match.group(2) else self.state.source_size - 1
        if start >= self.state.source_size or requested_end < start:
            self.send_empty(request_id, 416, "invalid_range")
            return
        end = min(requested_end, self.state.source_size - 1)
        length = end - start + 1
        if not self.state.reserve(length):
            self.send_empty(request_id, 509, "budget_exceeded")
            return

        actual_bytes_sent = 0
        violation = None
        partial_write = False
        try:
            self.send_response(206)
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Range", "bytes {}-{}/{}".format(
                start, end, self.state.source_size))
            self.send_header("Content-Length", str(length))
            self.send_header("Content-Type", "image/tiff")
            self.end_headers()
            with self.state.source_path.open("rb") as stream:
                stream.seek(start)
                remaining = length
                while remaining:
                    payload = stream.read(min(remaining, WRITE_CHUNK_BYTES))
                    if not payload:
                        raise OSError("short fixture read")
                    written = self.wfile.write(payload)
                    if written != len(payload):
                        raise OSError("short response write")
                    self.wfile.flush()
                    actual_bytes_sent += written
                    remaining -= written
        except (BrokenPipeError, ConnectionError, OSError) as error:
            partial_write = True
            violation = "partial_write:{}".format(type(error).__name__)
        self.state.complete(
            request_id, "GET", self.path, range_header, 206, length,
            actual_bytes_sent, reserved_bytes=length, violation=violation,
            partial_write=partial_write)


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
