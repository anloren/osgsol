#!/usr/bin/env python3
"""Build the release-time compact AlphaEarth SQLite/RTree index."""

import argparse
import csv
import datetime
import hashlib
import json
import math
import os
import posixpath
import re
import sqlite3
import sys
import tempfile
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit


SCHEMA_VERSION = 1
MIN_YEAR = 2017
MAX_YEAR = 2025
SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
GENERATED_AT_PATTERN = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
CSV_COLUMNS = [
    "dataset_id", "year", "cog_url", "vrt_url", "min_lon", "min_lat",
    "max_lon", "max_lat", "source_version", "source_checksum",
]


class ValidationError(ValueError):
    pass


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default="-", help="CSV input path, or - for stdin")
    parser.add_argument("--output", required=True, help="SQLite output path")
    parser.add_argument("--schema", default=str(Path(__file__).with_name("aef_index_schema.sql")))
    parser.add_argument("--schema-version", required=True, type=int)
    parser.add_argument("--source-index-url", required=True)
    parser.add_argument("--source-index-sha256", required=True)
    parser.add_argument("--expected-row-count", required=True, type=int)
    parser.add_argument("--generated-at")
    return parser.parse_args()


def validate_https_url(value, label):
    parsed = urlsplit(value)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ValidationError("{} has unsupported URL scheme (HTTPS required)".format(label))
    if parsed.username or parsed.password:
        raise ValidationError("{} must not contain credentials".format(label))
    return parsed


def validate_sha256(value, label):
    if not SHA256_PATTERN.fullmatch(value or ""):
        raise ValidationError("{} checksum must be 64 hexadecimal characters".format(label))
    return value.lower()


def parse_number(row, row_number, field):
    try:
        value = float(row[field])
    except (TypeError, ValueError):
        raise ValidationError("row {} {} is not numeric".format(row_number, field))
    if not math.isfinite(value):
        raise ValidationError("row {} {} must be finite".format(row_number, field))
    return value


def validate_row(row, row_number):
    dataset_id = row["dataset_id"].strip()
    if not dataset_id:
        raise ValidationError("row {} dataset_id is empty".format(row_number))
    if dataset_id != row["dataset_id"]:
        raise ValidationError("row {} dataset_id has surrounding whitespace".format(row_number))

    try:
        year = int(row["year"])
    except (TypeError, ValueError):
        raise ValidationError("row {} year is not an integer".format(row_number))
    if str(year) != row["year"].strip() or not MIN_YEAR <= year <= MAX_YEAR:
        raise ValidationError(
            "row {} year must be an integer from {} through {}".format(
                row_number, MIN_YEAR, MAX_YEAR))

    cog_url = row["cog_url"].strip()
    vrt_url = row["vrt_url"].strip()
    validate_https_url(cog_url, "row {} cog_url".format(row_number))
    validate_https_url(vrt_url, "row {} vrt_url".format(row_number))

    min_lon = parse_number(row, row_number, "min_lon")
    min_lat = parse_number(row, row_number, "min_lat")
    max_lon = parse_number(row, row_number, "max_lon")
    max_lat = parse_number(row, row_number, "max_lat")
    if not -180.0 <= min_lon <= 180.0 or not -180.0 <= max_lon <= 180.0:
        raise ValidationError("row {} longitude is outside [-180, 180]".format(row_number))
    if not -90.0 <= min_lat <= 90.0 or not -90.0 <= max_lat <= 90.0:
        raise ValidationError("row {} latitude is outside [-90, 90]".format(row_number))
    if min_lon > max_lon or min_lat > max_lat:
        raise ValidationError("row {} bbox minimum exceeds maximum".format(row_number))

    source_version = row["source_version"].strip()
    if not source_version:
        raise ValidationError("row {} source_version is empty".format(row_number))
    source_checksum = validate_sha256(
        row["source_checksum"].strip(), "row {} source".format(row_number))
    return {
        "dataset_id": dataset_id,
        "year": year,
        "cog_url": cog_url,
        "vrt_url": vrt_url,
        "min_lon": min_lon,
        "min_lat": min_lat,
        "max_lon": max_lon,
        "max_lat": max_lat,
        "source_version": source_version,
        "source_checksum": source_checksum,
    }


def read_rows(input_path):
    close_stream = input_path != "-"
    stream = open(input_path, newline="", encoding="utf-8") if close_stream else sys.stdin
    try:
        reader = csv.DictReader(stream)
        if reader.fieldnames != CSV_COLUMNS:
            raise ValidationError(
                "CSV columns do not match required schema: {}".format(",".join(CSV_COLUMNS)))
        rows = []
        seen_dataset_ids = set()
        for row_number, raw_row in enumerate(reader, start=2):
            if None in raw_row:
                raise ValidationError("row {} has extra CSV fields".format(row_number))
            row = validate_row(raw_row, row_number)
            if row["dataset_id"] in seen_dataset_ids:
                raise ValidationError(
                    "row {} duplicate dataset_id: {}".format(row_number, row["dataset_id"]))
            seen_dataset_ids.add(row["dataset_id"])
            rows.append(row)
        return sorted(rows, key=lambda row: (row["year"], row["dataset_id"]))
    finally:
        if close_stream:
            stream.close()


def compact_asset_urls(rows):
    parsed_urls = [urlsplit(row[field]) for row in rows for field in ("cog_url", "vrt_url")]
    if not parsed_urls:
        return ""
    origin = (parsed_urls[0].scheme, parsed_urls[0].netloc)
    if any((item.scheme, item.netloc) != origin or item.query or item.fragment
           for item in parsed_urls):
        return ""
    common_path = posixpath.commonpath([posixpath.dirname(item.path) for item in parsed_urls])
    if not common_path.endswith("/"):
        common_path += "/"
    asset_base_url = urlunsplit((origin[0], origin[1], common_path, "", ""))
    for row in rows:
        for field in ("cog_url", "vrt_url"):
            row[field] = urlsplit(row[field]).path[len(common_path):]
    return asset_base_url


def validate_generated_at(value):
    if value is None:
        return datetime.datetime.now(datetime.timezone.utc).replace(
            microsecond=0).isoformat().replace("+00:00", "Z")
    if not GENERATED_AT_PATTERN.fullmatch(value):
        raise ValidationError("generated_at must use YYYY-MM-DDTHH:MM:SSZ")
    datetime.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    return value


def build_database(arguments, rows, asset_base_url, generated_at, source_sha256):
    output = Path(arguments.output).resolve()
    schema_path = Path(arguments.schema)
    if arguments.schema_version != SCHEMA_VERSION:
        raise ValidationError("unsupported schema version: {}".format(arguments.schema_version))
    if arguments.expected_row_count < 0 or len(rows) != arguments.expected_row_count:
        raise ValidationError(
            "row count mismatch: expected {}, got {}".format(
                arguments.expected_row_count, len(rows)))
    schema_sql = schema_path.read_text(encoding="utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    file_descriptor, temporary_name = tempfile.mkstemp(
        prefix="." + output.name + ".", suffix=".tmp", dir=str(output.parent))
    os.close(file_descriptor)
    temporary_path = Path(temporary_name)
    try:
        connection = sqlite3.connect(str(temporary_path))
        try:
            connection.execute("PRAGMA journal_mode = DELETE")
            connection.execute("PRAGMA synchronous = FULL")
            connection.executescript(schema_sql)
            database_schema_version = connection.execute(
                "PRAGMA user_version").fetchone()[0]
            if database_schema_version != arguments.schema_version:
                raise ValidationError(
                    "schema version mismatch: requested {}, SQL declares {}".format(
                        arguments.schema_version, database_schema_version))
            for row_id, row in enumerate(rows, start=1):
                connection.execute(
                    """
                    INSERT INTO tiles
                    (id, dataset_id, year, cog_url, vrt_url, min_lon, min_lat,
                     max_lon, max_lat, source_version, source_checksum)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (row_id, row["dataset_id"], row["year"], row["cog_url"],
                     row["vrt_url"], row["min_lon"], row["min_lat"], row["max_lon"],
                     row["max_lat"], row["source_version"], row["source_checksum"]),
                )
                connection.execute(
                    "INSERT INTO tile_rtree VALUES (?, ?, ?, ?, ?)",
                    (row_id, row["min_lon"], row["max_lon"], row["min_lat"], row["max_lat"]),
                )
            connection.execute(
                "INSERT INTO metadata VALUES (?, ?, ?, ?, ?, ?)",
                (arguments.schema_version, arguments.source_index_url, source_sha256,
                 generated_at, len(rows), asset_base_url),
            )
            connection.commit()
            connection.execute("VACUUM")
            integrity = connection.execute("PRAGMA integrity_check").fetchone()[0]
            if integrity != "ok":
                raise RuntimeError("SQLite integrity_check failed: {}".format(integrity))
            if connection.execute("SELECT COUNT(*) FROM tiles").fetchone()[0] != len(rows):
                raise RuntimeError("SQLite row count validation failed")
            if connection.execute("SELECT COUNT(*) FROM tile_rtree").fetchone()[0] != len(rows):
                raise RuntimeError("SQLite RTree row count validation failed")
        finally:
            connection.close()
        with temporary_path.open("rb") as database_stream:
            os.fsync(database_stream.fileno())
        os.replace(temporary_path, output)
        directory_descriptor = os.open(str(output.parent), os.O_RDONLY)
        try:
            os.fsync(directory_descriptor)
        finally:
            os.close(directory_descriptor)
    finally:
        temporary_path.unlink(missing_ok=True)
    return output


def main():
    arguments = parse_arguments()
    try:
        validate_https_url(arguments.source_index_url, "source_index_url")
        source_sha256 = validate_sha256(
            arguments.source_index_sha256, "source index")
        generated_at = validate_generated_at(arguments.generated_at)
        rows = read_rows(arguments.input)
        asset_base_url = compact_asset_urls(rows)
        output = build_database(
            arguments, rows, asset_base_url, generated_at, source_sha256)
        output_bytes = output.read_bytes()
        json.dump({
            "schema_version": arguments.schema_version,
            "source_index_url": arguments.source_index_url,
            "source_sha256": source_sha256,
            "generated_at": generated_at,
            "row_count": len(rows),
            "output_sha256": hashlib.sha256(output_bytes).hexdigest(),
            "output_size": len(output_bytes),
        }, sys.stdout, sort_keys=True)
        sys.stdout.write("\n")
    except (OSError, sqlite3.Error, ValidationError, RuntimeError) as error:
        print("build_aef_index.py: error: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
