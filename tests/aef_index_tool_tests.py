#!/usr/bin/env python3

import csv
import hashlib
import json
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = REPO_ROOT / "tools" / "science"
BUILDER = TOOLS_DIR / "build_aef_index.py"
EXTRACTOR = TOOLS_DIR / "extract_aef_index.sh"
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
                "id", "dataset_id", "year", "cog_url", "vrt_url",
                "min_lon", "min_lat", "max_lon", "max_lat",
                "source_version", "source_checksum",
            ])
            metadata_columns = [
                row[1] for row in connection.execute("PRAGMA table_info(metadata)")
            ]
            self.assertEqual(metadata_columns, [
                "schema_version", "source_index_url", "source_index_sha256",
                "generated_at", "row_count", "asset_base_url",
            ])

            for query in self.expected["queries"].values():
                actual = self.query_point(
                    connection, query["latitude"], query["longitude"])
                self.assertEqual([list(row) for row in actual], query["rows"])

            ordered_rows = connection.execute(
                "SELECT id, dataset_id, year, cog_url, vrt_url FROM tiles ORDER BY id"
            ).fetchall()
            self.assertEqual(
                [(row[0], row[1], row[2]) for row in ordered_rows],
                [
                    (1, "zero-boundary-2017", 2017),
                    (2, "hong-kong-2023", 2023),
                    (3, "nvidia-2024", 2024),
                    (4, "hong-kong-2025", 2025),
                    (5, "nvidia-2025", 2025),
                ],
            )
            self.assertTrue(all(not row[3].startswith("http") for row in ordered_rows))
            self.assertTrue(all(not row[4].startswith("http") for row in ordered_rows))

            metadata = connection.execute("SELECT * FROM metadata").fetchone()
            self.assertEqual(metadata, (
                self.expected["schema_version"],
                self.expected["source_index_url"],
                self.source_sha256,
                self.expected["generated_at"],
                self.expected["row_count"],
                self.expected["asset_base_url"],
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
            self.assertEqual(actual[0], (1, "zero-boundary-2017", 2017))
            self.assertEqual(actual[-1], (5, "nvidia-2025", 2025))

    def test_builder_rejects_malformed_duplicate_and_mismatched_metadata_atomically(self):
        invalid_cases = []
        malformed = dict(self.expected["malformed_row"])
        invalid_cases.append(("year", self.valid_rows + [malformed], 6, "year"))
        invalid_cases.append((
            "duplicate", self.valid_rows + [self.expected["duplicate_row"]], 6,
            "duplicate dataset_id",
        ))
        override_cases = [
            ("longitude", "min_lon", "-181", "longitude"),
            ("latitude", "max_lat", "91", "latitude"),
            ("bbox", "max_lon", "-123", "bbox"),
            ("url", "cog_url", "ftp://example.invalid/bad.tif", "URL scheme"),
            ("checksum", "source_checksum", "not-a-sha256", "checksum"),
            ("version", "source_version", "", "source_version"),
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
                    "PRAGMA user_version = 1", "PRAGMA user_version = 2"),
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

    def test_production_projection_matches_pinned_parquet_schema(self):
        result = subprocess.run(
            ["bash", str(EXTRACTOR), "--print-duckdb-sql", "/tmp/aef_index.parquet"],
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        sql = result.stdout.decode()
        for field in (
                "fid", "path", "year", "wgs84_west", "wgs84_south",
                "wgs84_east", "wgs84_north"):
            self.assertIn(field, sql)
        for stale_field in ("datetime", "assets.data.href", "bbox.xmin"):
            self.assertNotIn(stale_field, sql)
        self.assertIn("s3://us-west-2.opendata.source.coop/", sql)
        self.assertIn("https://data.source.coop/", sql)
        self.assertIn("\\.tiff$", sql)
        self.assertIn(".vrt", sql)


if __name__ == "__main__":
    unittest.main()
