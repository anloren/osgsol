PRAGMA application_id = 0x41454632;
PRAGMA user_version = 2;

CREATE TABLE tiles
(
    id INTEGER PRIMARY KEY,
    dataset_id TEXT NOT NULL UNIQUE,
    year INTEGER NOT NULL CHECK(year BETWEEN 2017 AND 2025),
    cog_path TEXT NOT NULL,
    vrt_strategy TEXT NOT NULL CHECK(vrt_strategy = 'synthesize_vertical_flip'),
    min_lon REAL NOT NULL CHECK(min_lon BETWEEN -180.0 AND 180.0),
    min_lat REAL NOT NULL CHECK(min_lat BETWEEN -90.0 AND 90.0),
    max_lon REAL NOT NULL CHECK(max_lon BETWEEN -180.0 AND 180.0),
    max_lat REAL NOT NULL CHECK(max_lat BETWEEN -90.0 AND 90.0),
    source_version TEXT NOT NULL,
    record_fingerprint TEXT NOT NULL CHECK(length(record_fingerprint) = 64),
    CHECK(min_lon <= max_lon),
    CHECK(min_lat <= max_lat)
);

CREATE VIRTUAL TABLE tile_rtree USING rtree
(
    id,
    min_lon, max_lon,
    min_lat, max_lat
);

CREATE TABLE metadata
(
    schema_version INTEGER NOT NULL CHECK(schema_version = 2),
    source_index_url TEXT NOT NULL,
    source_index_sha256 TEXT NOT NULL CHECK(length(source_index_sha256) = 64),
    generated_at TEXT NOT NULL,
    row_count INTEGER NOT NULL CHECK(row_count >= 0),
    asset_base_url TEXT NOT NULL,
    record_fingerprint_algorithm TEXT NOT NULL CHECK(record_fingerprint_algorithm = 'sha256'),
    record_fingerprint_domain TEXT NOT NULL
        CHECK(record_fingerprint_domain = 'osgsol.aef.raw-index-record.v1'),
    record_fingerprint_canonicalization TEXT NOT NULL
        CHECK(record_fingerprint_canonicalization =
              'json-sort-keys-compact-utf8-numeric-17g-v1')
);

CREATE INDEX tiles_year_id ON tiles(year, id);
