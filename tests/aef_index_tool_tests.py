#!/usr/bin/env python3

import csv
import hashlib
import json
import sqlite3
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = REPO_ROOT / "tools" / "science"
BUILDER = TOOLS_DIR / "build_aef_index.py"
EXTRACTOR = TOOLS_DIR / "extract_aef_index.sh"
FETCH_DUCKDB = TOOLS_DIR / "fetch_duckdb.sh"
SCHEMA = TOOLS_DIR / "aef_index_schema.sql"
FIXTURE = REPO_ROOT / "tests" / "data" / "science" / "aef_index_fixture.csv"
EXPECTED_PATH = REPO_ROOT / "tests" / "data" / "science" / "aef_index_expected.json"


class AlphaEarthIndexToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.expected = json.loads(EXPECTED_PATH.read_text(encoding="utf-8"))
        cls.source_sha256 = hashlib.sha256(FIXTURE.read_bytes()).hexdigest()
        with FIXTURE.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            cls.fieldnames = reader.fieldnames
            cls.valid_rows = list(reader)

    def run_builder(self, output, input_path=FIXTURE, input_bytes=None,
                    expected_row_count=None, schema_path=SCHEMA,
                    schema_version=None, source_sha256=None):
        if expected_row_count is None:
            expected_row_count = self.expected["row_count"]
        if schema_version is None:
            schema_version = self.expected["schema_version"]
        if source_sha256 is None:
            source_sha256 = self.source_sha256
        command = [
            sys.executable,
            str(BUILDER),
            "--input", "-" if input_bytes is not None else str(input_path),
            "--output", str(output),
            "--schema", str(schema_path),
            "--schema-version", str(schema_version),
            "--source-index-url", self.expected["source_index_url"],
            "--source-index-sha256", source_sha256,
            "--expected-row-count", str(expected_row_count),
            "--generated-at", self.expected["generated_at"],
        ]
        return subprocess.run(
            command,
            cwd=REPO_ROOT,
            input=input_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def write_rows(self, path, rows):
        with path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=self.fieldnames)
            writer.writeheader()
            writer.writerows(rows)

    def query_point(self, connection, latitude, longitude):
        return connection.execute(
            """
            SELECT tiles.year, tiles.id, tiles.dataset_id
              FROM tile_rtree
              JOIN tiles ON tiles.id = tile_rtree.id
             WHERE tile_rtree.min_lon <= ? AND tile_rtree.max_lon >= ?
               AND tile_rtree.min_lat <= ? AND tile_rtree.max_lat >= ?
             ORDER BY tiles.year, tiles.id
            """,
            (longitude, longitude, latitude, latitude),
        ).fetchall()

    def test_builder_creates_compact_deterministic_rtree_database(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "alphaearth.sqlite"
            result = self.run_builder(output)
            self.assertEqual(result.returncode, 0, result.stderr.decode())

            connection = sqlite3.connect(output)
            self.addCleanup(connection.close)
            self.assertEqual(connection.execute("PRAGMA integrity_check").fetchone()[0], "ok")

            tiles_columns = [
                row[1] for row in connection.execute("PRAGMA table_info(tiles)")
            ]
            self.assertEqual(tiles_columns, [
                "id", "dataset_id", "year", "cog_path", "vrt_strategy",
                "min_lon", "min_lat", "max_lon", "max_lat",
                "source_version", "record_fingerprint",
            ])
            metadata_columns = [
                row[1] for row in connection.execute("PRAGMA table_info(metadata)")
            ]
            self.assertEqual(metadata_columns, [
                "schema_version", "source_index_url", "source_index_sha256",
                "generated_at", "row_count", "asset_base_url",
                "record_fingerprint_algorithm", "record_fingerprint_domain",
                "record_fingerprint_canonicalization",
            ])

            for query in self.expected["queries"].values():
                actual = self.query_point(
                    connection, query["latitude"], query["longitude"])
                self.assertEqual([list(row) for row in actual], query["rows"])

            ordered_rows = connection.execute(
                "SELECT id, dataset_id, year, cog_path, vrt_strategy, "
                "record_fingerprint FROM tiles ORDER BY id"
            ).fetchall()
            self.assertEqual(
                [(row[0], row[1], row[2]) for row in ordered_rows],
                [
                    (1, "1001", 2017),
                    (2, "1002", 2023),
                    (3, "1003", 2024),
                    (4, "1004", 2025),
                    (5, "1005", 2025),
                ],
            )
            self.assertTrue(all(not row[3].startswith(("http", "s3:")) for row in ordered_rows))
            self.assertTrue(all(row[4] == self.expected["vrt_strategy"] for row in ordered_rows))
            self.assertTrue(all(len(row[5]) == 64 for row in ordered_rows))
            self.assertEqual(
                {row[1]: row[5] for row in ordered_rows},
                self.expected["record_fingerprints"],
            )

            metadata = connection.execute("SELECT * FROM metadata").fetchone()
            self.assertEqual(metadata, (
                self.expected["schema_version"],
                self.expected["source_index_url"],
                self.source_sha256,
                self.expected["generated_at"],
                self.expected["row_count"],
                self.expected["asset_base_url"],
                self.expected["record_fingerprint_algorithm"],
                self.expected["record_fingerprint_domain"],
                self.expected["record_fingerprint_canonicalization"],
            ))
            self.assertEqual(
                connection.execute("SELECT COUNT(*) FROM tile_rtree").fetchone()[0],
                self.expected["row_count"],
            )

    def test_builder_accepts_stdin_and_assigns_the_same_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            shuffled = list(reversed(self.valid_rows))
            csv_path = Path(directory) / "shuffled.csv"
            self.write_rows(csv_path, shuffled)
            output = Path(directory) / "alphaearth.sqlite"
            result = self.run_builder(output, input_bytes=csv_path.read_bytes())
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            connection = sqlite3.connect(output)
            try:
                actual = connection.execute(
                    "SELECT id, dataset_id, year FROM tiles ORDER BY id"
                ).fetchall()
            finally:
                connection.close()
            self.assertEqual(actual[0], (1, "1001", 2017))
            self.assertEqual(actual[-1], (5, "1005", 2025))

    def test_builder_rejects_malformed_duplicate_and_mismatched_metadata_atomically(self):
        invalid_cases = []
        malformed = dict(self.expected["malformed_row"])
        invalid_cases.append(("year", self.valid_rows + [malformed], 6, "year"))
        invalid_cases.append((
            "duplicate", self.valid_rows + [self.expected["duplicate_row"]], 6,
            "duplicate fid",
        ))
        override_cases = [
            ("longitude", "wgs84_west", "-181", "longitude"),
            ("latitude", "wgs84_north", "91", "latitude"),
            ("bbox", "wgs84_east", "-123", "bbox"),
            ("origin", "path", "s3://evil.example/aef/bad.tiff", "origin"),
            ("query", "path", self.valid_rows[0]["path"] + "?token=bad", "query"),
            ("fragment", "path", self.valid_rows[0]["path"] + "#bad", "fragment"),
            ("noncompact", "path", self.valid_rows[0]["path"].replace(
                "/2025/", "/2025/../2025/"), "compact"),
            ("encoded", "path", self.valid_rows[0]["path"].replace(
                "/2025/", "/2025/%2e%2e/"), "encoded"),
            ("backslash", "path", self.valid_rows[0]["path"].replace(
                "/2025/", "/2025/bad\\segment/"), "backslash"),
            ("control", "path", self.valid_rows[0]["path"].replace(
                "/2025/", "/2025/bad\nsegment/"), "control"),
            ("location", "location", "VRT://vsis3/evil.example/bad.tiff", "location"),
        ]
        for name, field, value, message in override_cases:
            rows = [dict(row) for row in self.valid_rows]
            rows[0][field] = value
            invalid_cases.append((name, rows, 5, message))

        with tempfile.TemporaryDirectory() as directory:
            for name, rows, expected_count, message in invalid_cases:
                with self.subTest(name=name):
                    csv_path = Path(directory) / (name + ".csv")
                    output = Path(directory) / (name + ".sqlite")
                    output.write_bytes(b"existing-database-must-survive")
                    self.write_rows(csv_path, rows)
                    result = self.run_builder(
                        output, input_path=csv_path,
                        expected_row_count=expected_count)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(message, result.stderr.decode())
                    self.assertEqual(output.read_bytes(), b"existing-database-must-survive")

            output = Path(directory) / "row-count.sqlite"
            result = self.run_builder(output, expected_row_count=99)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("row count", result.stderr.decode())
            self.assertFalse(output.exists())

    def test_builder_rejects_schema_version_mismatch_atomically(self):
        with tempfile.TemporaryDirectory() as directory:
            mismatched_schema = Path(directory) / "schema-v2.sql"
            mismatched_schema.write_text(
                SCHEMA.read_text(encoding="utf-8").replace(
                    "PRAGMA user_version = 2", "PRAGMA user_version = 3"),
                encoding="utf-8",
            )
            output = Path(directory) / "alphaearth.sqlite"
            output.write_bytes(b"existing-database-must-survive")
            result = self.run_builder(output, schema_path=mismatched_schema)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("schema version", result.stderr.decode())
            self.assertEqual(output.read_bytes(), b"existing-database-must-survive")

    def test_fixture_extractor_writes_database_and_checksum_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "alphaearth.sqlite"
            result = subprocess.run(
                [
                    "bash", str(EXTRACTOR),
                    "--fixture", str(FIXTURE),
                    "--output", str(output),
                ],
                cwd=REPO_ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            manifest_path = Path(str(output) + ".manifest.json")
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema_version"], self.expected["schema_version"])
            self.assertEqual(manifest["row_count"], self.expected["row_count"])
            self.assertEqual(manifest["source_sha256"], self.source_sha256)
            self.assertEqual(manifest["output_sha256"], hashlib.sha256(output.read_bytes()).hexdigest())
            self.assertEqual(manifest["output_size"], output.stat().st_size)

    def test_production_projection_exports_only_raw_primary_fields(self):
        result = subprocess.run(
            ["bash", str(EXTRACTOR), "--print-duckdb-sql", "/tmp/aef_index.parquet"],
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        sql = result.stdout.decode()
        normalized = " ".join(sql.split())
        self.assertIn(
            "SELECT fid, path, year, wgs84_west, wgs84_south, "
            "wgs84_east, wgs84_north, location", normalized)
        for transformed_field in (
                "dataset_id", "cog_url", "cog_path", "vrt_url", "vrt_strategy",
                "record_fingerprint", "sha256("):
            self.assertNotIn(transformed_field, sql)

    def test_fetch_duckdb_replaces_poisoned_cached_archive_atomically(self):
        build_root = REPO_ROOT / "build" / "science-tools" / "tests"
        build_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=build_root) as directory:
            root = Path(directory)
            source_archive = root / "source" / "duckdb.zip"
            source_archive.parent.mkdir()
            with zipfile.ZipFile(source_archive, "w") as archive:
                archive.writestr("duckdb", "#!/usr/bin/env bash\nexit 0\n")
            archive_bytes = source_archive.read_bytes()
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "duckdb": {
                    "version": "test",
                    "archive": "duckdb.zip",
                    "url": source_archive.as_uri(),
                    "sha256": hashlib.sha256(archive_bytes).hexdigest(),
                    "size": len(archive_bytes),
                    "binary": "duckdb",
                }
            }), encoding="utf-8")
            tools_dir = root / "tools"
            cached_archive = tools_dir / "downloads" / "duckdb.zip"
            cached_archive.parent.mkdir(parents=True)
            cached_archive.write_bytes(b"poisoned-cache")

            result = subprocess.run(
                ["bash", str(FETCH_DUCKDB), "--manifest", str(manifest),
                 "--tools-dir", str(tools_dir)],
                cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(cached_archive.read_bytes(), archive_bytes)
            self.assertTrue((tools_dir / "duckdb").is_file())
            self.assertFalse(list(tools_dir.rglob("*.part*")))

    def test_extract_replaces_poisoned_source_cache_before_use(self):
        build_root = REPO_ROOT / "build" / "science-tools" / "tests"
        build_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=build_root) as directory:
            root = Path(directory)
            source = root / "source" / "aef_index.parquet"
            source.parent.mkdir()
            source.write_bytes(b"PAR1-tiny-source-fixture-PAR1")
            source_bytes = source.read_bytes()
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "source_index": {
                    "url": source.as_uri(),
                    "sha256": hashlib.sha256(source_bytes).hexdigest(),
                    "size": len(source_bytes),
                }
            }), encoding="utf-8")
            tools_dir = root / "tools"
            tools_dir.mkdir()
            cached_source = tools_dir / "aef_index.parquet"
            cached_source.write_bytes(b"poisoned-cache")

            result = subprocess.run(
                ["bash", str(EXTRACTOR), "--fetch-source-only",
                 "--manifest", str(manifest), "--tools-dir", str(tools_dir)],
                cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(cached_source.read_bytes(), source_bytes)
            self.assertFalse(list(tools_dir.rglob("*.part*")))

    def test_fetch_rejects_manifest_path_traversal_without_touching_sentinel(self):
        build_root = REPO_ROOT / "build" / "science-tools" / "tests"
        build_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=build_root) as directory:
            root = Path(directory)
            source_archive = root / "source.zip"
            with zipfile.ZipFile(source_archive, "w") as archive:
                archive.writestr("duckdb", "#!/usr/bin/env bash\nexit 0\n")
            archive_bytes = source_archive.read_bytes()
            cases = [
                ("archive", "../sentinel.zip", "duckdb", "sentinel.zip"),
                ("binary", "duckdb.zip", "../sentinel", "sentinel"),
            ]
            for label, archive_name, binary_name, sentinel_name in cases:
                with self.subTest(label=label):
                    case_root = root / label
                    case_root.mkdir()
                    manifest = case_root / "manifest.json"
                    manifest.write_text(json.dumps({
                        "duckdb": {
                            "version": "test", "archive": archive_name,
                            "url": source_archive.as_uri(),
                            "sha256": hashlib.sha256(archive_bytes).hexdigest(),
                            "size": len(archive_bytes), "binary": binary_name,
                        }
                    }), encoding="utf-8")
                    tools_dir = case_root / "tools"
                    tools_dir.mkdir()
                    sentinel = (tools_dir if label == "archive" else case_root) / sentinel_name
                    sentinel.write_bytes(b"must-survive")
                    result = subprocess.run(
                        ["bash", str(FETCH_DUCKDB), "--manifest", str(manifest),
                         "--tools-dir", str(tools_dir)],
                        cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        check=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(label, result.stderr.decode())
                    self.assertEqual(sentinel.read_bytes(), b"must-survive")

    def test_scripts_reject_external_tools_directory_without_creating_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cases = [
                ("fetch", ["bash", str(FETCH_DUCKDB), "--tools-dir"]),
                ("extract", ["bash", str(EXTRACTOR), "--fetch-source-only", "--tools-dir"]),
            ]
            for name, prefix in cases:
                with self.subTest(name=name):
                    candidate = root / name / "must-not-exist"
                    result = subprocess.run(
                        prefix + [str(candidate)], cwd=REPO_ROOT,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("build/science-tools", result.stderr.decode())
                    self.assertFalse(candidate.exists())

    def test_failed_cache_download_never_reaches_final_path(self):
        build_root = REPO_ROOT / "build" / "science-tools" / "tests"
        build_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=build_root) as directory:
            root = Path(directory)
            invalid_source = root / "invalid-download"
            invalid_source.write_bytes(b"wrong-bytes")

            fetch_tools = root / "fetch-tools"
            fetch_manifest = root / "fetch-manifest.json"
            fetch_manifest.write_text(json.dumps({
                "duckdb": {
                    "version": "test", "archive": "duckdb.zip",
                    "url": invalid_source.as_uri(), "sha256": "a" * 64,
                    "size": 123, "binary": "duckdb",
                }
            }), encoding="utf-8")
            fetch_result = subprocess.run(
                ["bash", str(FETCH_DUCKDB), "--manifest", str(fetch_manifest),
                 "--tools-dir", str(fetch_tools)], cwd=REPO_ROOT,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(fetch_result.returncode, 0)
            self.assertFalse((fetch_tools / "downloads" / "duckdb.zip").exists())
            self.assertFalse(list(fetch_tools.rglob("*.part*")))

            extract_tools = root / "extract-tools"
            extract_manifest = root / "extract-manifest.json"
            extract_manifest.write_text(json.dumps({
                "source_index": {
                    "url": invalid_source.as_uri(), "sha256": "b" * 64,
                    "size": 456,
                }
            }), encoding="utf-8")
            extract_result = subprocess.run(
                ["bash", str(EXTRACTOR), "--fetch-source-only",
                 "--manifest", str(extract_manifest),
                 "--tools-dir", str(extract_tools)], cwd=REPO_ROOT,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(extract_result.returncode, 0)
            self.assertFalse((extract_tools / "aef_index.parquet").exists())
            self.assertFalse(list(extract_tools.rglob("*.part*")))


if __name__ == "__main__":
    unittest.main()
