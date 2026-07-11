#!/usr/bin/env python3
"""Build the release-time compact AlphaEarth SQLite/RTree index."""

import argparse
import csv
import datetime
import hashlib
import json
import math
import os
import re
import sqlite3
import sys
import tempfile
from pathlib import Path
from urllib.parse import urlsplit


SCHEMA_VERSION = 2
MIN_YEAR = 2017
MAX_YEAR = 2025
SOURCE_VERSION = "1.1"
S3_ORIGIN = "us-west-2.opendata.source.coop"
S3_PREFIX = "s3://{}/tge-labs/aef/v1/annual/".format(S3_ORIGIN)
VRT_LOCATION_PREFIX = "VRT://vsis3/{}/tge-labs/aef/v1/annual/".format(S3_ORIGIN)
ASSET_BASE_URL = "https://data.source.coop/tge-labs/aef/v1/annual/"
VRT_STRATEGY = "synthesize_vertical_flip"
FINGERPRINT_ALGORITHM = "sha256"
FINGERPRINT_DOMAIN = "osgsol.aef.raw-index-record.v1"
FINGERPRINT_CANONICALIZATION = "json-sort-keys-compact-utf8-numeric-17g-v1"
SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
GENERATED_AT_PATTERN = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
CSV_COLUMNS = [
    "fid", "path", "year", "wgs84_west", "wgs84_south", "wgs84_east",
    "wgs84_north", "location",
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


def canonical_number(value):
    return format(value, ".17g")


def record_fingerprint(raw_record):
    canonical_record = json.dumps(
        raw_record, ensure_ascii=True, allow_nan=False, sort_keys=True,
        separators=(",", ":"))
    payload = (FINGERPRINT_DOMAIN + "\0" + canonical_record).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def validate_source_path(value, row_number, year):
    if value != value.strip():
        raise ValidationError("row {} path has surrounding whitespace".format(row_number))
    parsed = urlsplit(value)
    if parsed.query or parsed.fragment:
        raise ValidationError("row {} path query/fragment is forbidden".format(row_number))
    if parsed.scheme != "s3" or parsed.netloc != S3_ORIGIN:
        raise ValidationError("row {} path origin is not the trusted AEF source".format(row_number))
    if not value.startswith(S3_PREFIX):
        raise ValidationError("row {} path is outside the trusted AEF prefix".format(row_number))
    relative_path = value[len(S3_PREFIX):]
    parts = relative_path.split("/")
    if (not relative_path or relative_path.startswith("/") or "" in parts or
            any(part in (".", "..") for part in parts)):
        raise ValidationError("row {} path is not compact/canonical".format(row_number))
    if parts[0] != str(year):
        raise ValidationError("row {} path year does not match year column".format(row_number))
    if not relative_path.endswith(".tiff"):
        raise ValidationError("row {} path must name a .tiff COG".format(row_number))
    return relative_path


def validate_row(row, row_number):
    try:
        fid = int(row["fid"])
    except (TypeError, ValueError):
        raise ValidationError("row {} fid is not an integer".format(row_number))
    if fid < 0 or str(fid) != row["fid"].strip():
        raise ValidationError("row {} fid must be a canonical non-negative integer".format(row_number))

    try:
        year = int(row["year"])
    except (TypeError, ValueError):
        raise ValidationError("row {} year is not an integer".format(row_number))
    if str(year) != row["year"].strip() or not MIN_YEAR <= year <= MAX_YEAR:
        raise ValidationError(
            "row {} year must be an integer from {} through {}".format(
                row_number, MIN_YEAR, MAX_YEAR))

    relative_path = validate_source_path(row["path"], row_number, year)
    expected_location = VRT_LOCATION_PREFIX + relative_path
    if row["location"] != expected_location:
        raise ValidationError(
            "row {} location does not match the trusted path/orientation metadata".format(
                row_number))

    min_lon = parse_number(row, row_number, "wgs84_west")
    min_lat = parse_number(row, row_number, "wgs84_south")
    max_lon = parse_number(row, row_number, "wgs84_east")
    max_lat = parse_number(row, row_number, "wgs84_north")
    if not -180.0 <= min_lon <= 180.0 or not -180.0 <= max_lon <= 180.0:
        raise ValidationError("row {} longitude is outside [-180, 180]".format(row_number))
    if not -90.0 <= min_lat <= 90.0 or not -90.0 <= max_lat <= 90.0:
        raise ValidationError("row {} latitude is outside [-90, 90]".format(row_number))
    if min_lon > max_lon or min_lat > max_lat:
        raise ValidationError("row {} bbox minimum exceeds maximum".format(row_number))

    raw_record = {
        "fid": str(fid),
        "location": row["location"],
        "path": row["path"],
        "wgs84_east": canonical_number(max_lon),
        "wgs84_north": canonical_number(max_lat),
        "wgs84_south": canonical_number(min_lat),
        "wgs84_west": canonical_number(min_lon),
        "year": year,
    }
    return {
        "dataset_id": str(fid),
        "year": year,
        "cog_path": relative_path,
        "vrt_strategy": VRT_STRATEGY,
        "min_lon": min_lon,
        "min_lat": min_lat,
        "max_lon": max_lon,
        "max_lat": max_lat,
        "source_version": SOURCE_VERSION,
        "record_fingerprint": record_fingerprint(raw_record),
    }


def read_rows(input_path):
    close_stream = input_path != "-"
    stream = open(input_path, newline="", encoding="utf-8") if close_stream else sys.stdin
    try:
        reader = csv.DictReader(stream)
        if reader.fieldnames != CSV_COLUMNS:
            raise ValidationError(
                "CSV columns do not match primary schema: {}".format(",".join(CSV_COLUMNS)))
        rows = []
        seen_fids = set()
        for row_number, raw_row in enumerate(reader, start=2):
            if None in raw_row:
                raise ValidationError("row {} has extra CSV fields".format(row_number))
            row = validate_row(raw_row, row_number)
            if row["dataset_id"] in seen_fids:
                raise ValidationError(
                    "row {} duplicate fid: {}".format(row_number, row["dataset_id"]))
            seen_fids.add(row["dataset_id"])
            rows.append(row)
        return sorted(rows, key=lambda row: (row["year"], int(row["dataset_id"])))
    finally:
        if close_stream:
            stream.close()


def validate_generated_at(value):
    if value is None:
        return datetime.datetime.now(datetime.timezone.utc).replace(
            microsecond=0).isoformat().replace("+00:00", "Z")
    if not GENERATED_AT_PATTERN.fullmatch(value):
        raise ValidationError("generated_at must use YYYY-MM-DDTHH:MM:SSZ")
    datetime.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    return value


def build_database(arguments, rows, generated_at, source_sha256):
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
                    (id, dataset_id, year, cog_path, vrt_strategy, min_lon, min_lat,
                     max_lon, max_lat, source_version, record_fingerprint)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (row_id, row["dataset_id"], row["year"], row["cog_path"],
                     row["vrt_strategy"], row["min_lon"], row["min_lat"], row["max_lon"],
                     row["max_lat"], row["source_version"], row["record_fingerprint"]),
                )
                connection.execute(
                    "INSERT INTO tile_rtree VALUES (?, ?, ?, ?, ?)",
                    (row_id, row["min_lon"], row["max_lon"], row["min_lat"], row["max_lat"]),
                )
            connection.execute(
                "INSERT INTO metadata VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (arguments.schema_version, arguments.source_index_url, source_sha256,
                 generated_at, len(rows), ASSET_BASE_URL, FINGERPRINT_ALGORITHM,
                 FINGERPRINT_DOMAIN, FINGERPRINT_CANONICALIZATION),
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
        output = build_database(arguments, rows, generated_at, source_sha256)
        output_bytes = output.read_bytes()
        json.dump({
            "schema_version": arguments.schema_version,
            "source_index_url": arguments.source_index_url,
            "source_sha256": source_sha256,
            "generated_at": generated_at,
            "row_count": len(rows),
            "asset_base_url": ASSET_BASE_URL,
            "record_fingerprint_algorithm": FINGERPRINT_ALGORITHM,
            "record_fingerprint_domain": FINGERPRINT_DOMAIN,
            "record_fingerprint_canonicalization": FINGERPRINT_CANONICALIZATION,
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
