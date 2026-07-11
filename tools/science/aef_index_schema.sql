PRAGMA application_id = 0x41454631;
PRAGMA user_version = 1;

CREATE TABLE tiles
(
    id INTEGER PRIMARY KEY,
    dataset_id TEXT NOT NULL UNIQUE,
    year INTEGER NOT NULL CHECK(year BETWEEN 2017 AND 2025),
    cog_url TEXT NOT NULL,
    vrt_url TEXT NOT NULL,
    min_lon REAL NOT NULL CHECK(min_lon BETWEEN -180.0 AND 180.0),
    min_lat REAL NOT NULL CHECK(min_lat BETWEEN -90.0 AND 90.0),
    max_lon REAL NOT NULL CHECK(max_lon BETWEEN -180.0 AND 180.0),
    max_lat REAL NOT NULL CHECK(max_lat BETWEEN -90.0 AND 90.0),
    source_version TEXT NOT NULL,
    source_checksum TEXT NOT NULL CHECK(length(source_checksum) = 64),
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
    schema_version INTEGER NOT NULL CHECK(schema_version = 1),
    source_index_url TEXT NOT NULL,
    source_index_sha256 TEXT NOT NULL CHECK(length(source_index_sha256) = 64),
    generated_at TEXT NOT NULL,
    row_count INTEGER NOT NULL CHECK(row_count >= 0),
    asset_base_url TEXT NOT NULL
);

CREATE INDEX tiles_year_id ON tiles(year, id);
