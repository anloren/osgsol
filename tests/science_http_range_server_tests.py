import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("science_http_range_server.py")
SPEC = importlib.util.spec_from_file_location("science_http_range_server", MODULE_PATH)
SERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SERVER)


class FailAfterFirstChunk:
    def __init__(self):
        self.successful_bytes = 0
        self.write_calls = 0

    def write(self, payload):
        self.write_calls += 1
        if self.write_calls > 1:
            raise BrokenPipeError("intentional partial-write fixture")
        self.successful_bytes += len(payload)
        return len(payload)

    def flush(self):
        return None


class ScienceHttpRangeServerTests(unittest.TestCase):
    def test_partial_write_logs_only_completed_chunks_and_releases_reservation(self):
        with tempfile.TemporaryDirectory(prefix="osgsol-range-server-test-") as root:
            root_path = Path(root)
            fixture = root_path / "fixture.tif"
            log = root_path / "requests.jsonl"
            fixture.write_bytes(b"x" * 200_000)
            state = SERVER.RangeState(fixture, log, 300_000)

            handler = object.__new__(SERVER.StrictRangeHandler)
            handler.state = state
            handler.command = "GET"
            handler.path = "/fixture.tif"
            handler.headers = {"Range": "bytes=0-199999"}
            handler.wfile = FailAfterFirstChunk()
            handler.send_response = lambda unused_code: None
            handler.send_header = lambda unused_name, unused_value: None
            handler.end_headers = lambda: None

            handler.do_GET()

            entries = [json.loads(line) for line in log.read_text().splitlines()]
            completion = entries[-1]
            self.assertEqual(completion["event"], "request_complete")
            self.assertEqual(completion["planned_bytes"], 200_000)
            self.assertEqual(completion["actual_bytes_sent"], 65_536)
            self.assertEqual(completion["total_committed_bytes"], 65_536)
            self.assertEqual(completion["total_reserved_bytes"], 0)
            self.assertTrue(completion["partial_write"])
            self.assertTrue(completion["violation"].startswith("partial_write:"))


if __name__ == "__main__":
    unittest.main()
