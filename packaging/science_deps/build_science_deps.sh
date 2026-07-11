#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(cd "$script_dir/../.." && pwd -P)
versions_file="$script_dir/versions.env"
checksums_file="$script_dir/checksums.txt"

# shellcheck disable=SC1090
source "$versions_file"

science_root=${SCIENCE_DEPS_ROOT:-"$repo_root/build/science-deps"}
downloads_dir=${SCIENCE_DEPS_DOWNLOADS:-"$science_root/downloads"}
src_dir=${SCIENCE_DEPS_SRC:-"$science_root/src"}
build_dir=${SCIENCE_DEPS_BUILD:-"$science_root/build"}
prefix=${SCIENCE_DEPS_PREFIX:-"$science_root/prefix"}
manifest="$prefix/science-deps-manifest.json"
deployment_target=${SCIENCE_DEPS_DEPLOYMENT_TARGET:-11.0}
builtin_raster_drivers=(MEM)

usage()
{
    cat <<EOF
Usage: $0 --download-only | --build [--jobs N] | --verify

Environment overrides:
  SCIENCE_DEPS_ROOT       Workspace (default: build/science-deps)
  SCIENCE_DEPS_PREFIX     Private install prefix (default: ROOT/prefix)
  SCIENCE_DEPS_DEPLOYMENT_TARGET  macOS target (default: 11.0)
EOF
}

die()
{
    echo "[science-deps] error: $*" >&2
    exit 1
}

mode=
jobs=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --download-only|--build|--verify)
            [[ -z $mode ]] || die "choose exactly one action"
            mode=$1
            shift
            ;;
        --jobs)
            [[ $# -ge 2 ]] || die "--jobs requires a value"
            jobs=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "unknown argument: $1"
            ;;
    esac
done

[[ -n $mode ]] || { usage >&2; exit 2; }
[[ $jobs =~ ^[1-9][0-9]*$ ]] || die "jobs must be a positive integer"
[[ $mode == --build || $jobs == 1 ]] || die "--jobs is valid only with --build"

canonical_prefix=$(python3 - "$prefix" <<'PY'
import os
import sys
print(os.path.realpath(sys.argv[1]))
PY
)
case "$canonical_prefix" in
    /opt/homebrew|/opt/homebrew/*|/usr/local|/usr/local/*)
        die "refusing unsafe system or Homebrew prefix: $canonical_prefix"
        ;;
esac

for tool in cmake curl python3 shasum tar; do
    command -v "$tool" >/dev/null 2>&1 || die "required build tool not found: $tool"
done
[[ $(uname -s) == Darwin ]] || die "this pinned prefix recipe currently supports macOS only"
sdk_root=$(xcrun --show-sdk-path)
system_curl_header="$sdk_root/usr/include/curl/curl.h"
system_curl_library="$sdk_root/usr/lib/libcurl.tbd"
system_sqlite_header="$sdk_root/usr/include/sqlite3.h"
system_sqlite_library="$sdk_root/usr/lib/libsqlite3.tbd"
[[ -f $system_curl_header && -f $system_curl_library ]] ||
    die "macOS SDK curl was not found"
[[ -f $system_sqlite_header && -f $system_sqlite_library ]] ||
    die "macOS SDK SQLite was not found"

mkdir -p "$downloads_dir" "$src_dir" "$build_dir"

download_archive()
{
    local name=$1
    local url=$2
    local expected_bytes=$3
    local output="$downloads_dir/$name"
    local partial="$output.part"

    if [[ ! -f $output ]]; then
        echo "[science-deps] downloading $url"
        rm -f "$partial"
        curl --proto '=https' --tlsv1.2 -fL --retry 3 --retry-delay 1 \
            --output "$partial" "$url"
        mv "$partial" "$output"
    fi
    local actual_bytes
    actual_bytes=$(stat -f %z "$output")
    [[ $actual_bytes == "$expected_bytes" ]] ||
        die "$name has $actual_bytes bytes; expected $expected_bytes"
}

verify_archives()
{
    (
        cd "$downloads_dir"
        shasum -a 256 -c "$checksums_file"
    )
}

download_all()
{
    download_archive "$GDAL_ARCHIVE" "$GDAL_URL" "$GDAL_ARCHIVE_BYTES"
    download_archive "$PROJ_ARCHIVE" "$PROJ_URL" "$PROJ_ARCHIVE_BYTES"
    download_archive "$ZSTD_ARCHIVE" "$ZSTD_URL" "$ZSTD_ARCHIVE_BYTES"
    verify_archives
}

extract_archive()
{
    local archive=$1
    local extracted_name=$2
    local sentinel=$3
    local destination="$src_dir/$extracted_name"
    [[ -f "$src_dir/$sentinel" ]] && return
    rm -rf "$destination"
    tar -xzf "$downloads_dir/$archive" -C "$src_dir"
    [[ -f "$src_dir/$sentinel" ]] || die "archive did not extract $sentinel"
}

extract_all()
{
    local gdal_cmake_entry="gdal-$GDAL_VERSION/CMakeLists.txt"
    local proj_cmake_entry="proj-$PROJ_VERSION/CMakeLists.txt"
    local zstd_cmake_entry="zstd-$ZSTD_VERSION/build/cmake/CMakeLists.txt"
    extract_archive "$GDAL_ARCHIVE" "gdal-$GDAL_VERSION" "$gdal_cmake_entry"
    extract_archive "$PROJ_ARCHIVE" "proj-$PROJ_VERSION" "$proj_cmake_entry"
    extract_archive "$ZSTD_ARCHIVE" "zstd-$ZSTD_VERSION" "$zstd_cmake_entry"
}

configure_component()
{
    local name=$1
    shift
    local log="$science_root/configure-$name.log"
    echo "[science-deps] configuring $name"
    if ! cmake "$@" 2>&1 | tee "$log"; then
        die "$name configure failed"
    fi
    if grep -q 'Manually-specified variables were not used' "$log"; then
        die "$name configure ignored a requested CMake key"
    fi
}

cache_value()
{
    local cache=$1
    local key=$2
    local line
    line=$(grep -E "^${key}(:[^=]+)?=" "$cache" | tail -1) ||
        die "$key is absent from $cache"
    printf '%s\n' "${line#*=}"
}

cache_expect()
{
    local cache=$1
    local key=$2
    local expected=$3
    local actual
    actual=$(cache_value "$cache" "$key")
    [[ $actual == "$expected" ]] ||
        die "$key resolved to $actual in $cache; expected $expected"
}

verify_common_cache()
{
    local cache=$1
    cache_expect "$cache" CMAKE_BUILD_TYPE Release
    cache_expect "$cache" CMAKE_POSITION_INDEPENDENT_CODE ON
    cache_expect "$cache" CMAKE_C_VISIBILITY_PRESET hidden
    cache_expect "$cache" CMAKE_CXX_VISIBILITY_PRESET hidden
    cache_expect "$cache" CMAKE_OSX_DEPLOYMENT_TARGET "$deployment_target"
    cache_expect "$cache" CMAKE_INSTALL_PREFIX "$prefix"
}

common_cmake_args=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_C_VISIBILITY_PRESET=hidden
    -DCMAKE_CXX_VISIBILITY_PRESET=hidden
    -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target"
    -DCMAKE_INSTALL_LIBDIR=lib
)

build_zstd()
{
    local component_build="$build_dir/zstd"
    rm -rf "$component_build"
    configure_component zstd \
        -S "$src_dir/zstd-$ZSTD_VERSION/build/cmake" \
        -B "$component_build" \
        "${common_cmake_args[@]}" \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DZSTD_BUILD_STATIC=ON \
        -DZSTD_BUILD_SHARED=OFF \
        -DZSTD_BUILD_PROGRAMS=OFF \
        -DZSTD_BUILD_TESTS=OFF \
        -DZSTD_BUILD_CONTRIB=OFF
    cmake --build "$component_build" --target install --parallel "$jobs"
}

build_proj()
{
    local component_build="$build_dir/proj"
    rm -rf "$component_build"
    configure_component proj \
        -S "$src_dir/proj-$PROJ_VERSION" \
        -B "$component_build" \
        "${common_cmake_args[@]}" \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_APPS=OFF \
        -DBUILD_TESTING=OFF \
        -DBUILD_PROJSYNC=OFF \
        -DENABLE_TIFF=OFF \
        -DENABLE_CURL=OFF \
        -DEMBED_PROJ_DATA_PATH=OFF \
        -DEMBED_RESOURCE_FILES=ON \
        -DUSE_ONLY_EMBEDDED_RESOURCE_FILES=ON \
        -DSQLite3_INCLUDE_DIR="$sdk_root/usr/include" \
        -DSQLite3_LIBRARY="$system_sqlite_library"
    cmake --build "$component_build" --target install --parallel "$jobs"
}

build_gdal()
{
    local component_build="$build_dir/gdal"
    rm -rf "$component_build"
    configure_component gdal \
        -S "$src_dir/gdal-$GDAL_VERSION" \
        -B "$component_build" \
        "${common_cmake_args[@]}" \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_PREFIX_PATH="$prefix" \
        '-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local' \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_APPS=OFF \
        -DBUILD_PYTHON_BINDINGS=OFF \
        -DCMAKE_DISABLE_FIND_PACKAGE_SWIG=ON \
        -DBUILD_TESTING=OFF \
        -DGDAL_BUILD_OPTIONAL_DRIVERS=OFF \
        -DOGR_BUILD_OPTIONAL_DRIVERS=OFF \
        -DGDAL_ENABLE_DRIVER_GTIFF=ON \
        -DGDAL_ENABLE_DRIVER_VRT=ON \
        -DOGR_ENABLE_DRIVER_GEOJSON=OFF \
        -DOGR_ENABLE_DRIVER_SHAPE=OFF \
        -DGDAL_USE_EXTERNAL_LIBS=OFF \
        -DGDAL_USE_INTERNAL_LIBS=OFF \
        -DGDAL_USE_CURL=ON \
        -DGDAL_USE_SQLITE3=ON \
        -DGDAL_USE_ZSTD=ON \
        -DGDAL_USE_ZLIB=OFF \
        -DGDAL_USE_ZLIB_INTERNAL=ON \
        -DGDAL_USE_JPEG12_INTERNAL=OFF \
        -DENABLE_DEFLATE64=OFF \
        -DGDAL_USE_TIFF=OFF \
        -DGDAL_USE_TIFF_INTERNAL=ON \
        -DGDAL_USE_GEOTIFF=OFF \
        -DGDAL_USE_GEOTIFF_INTERNAL=ON \
        -DGDAL_USE_JSONC=OFF \
        -DGDAL_USE_JSONC_INTERNAL=ON \
        -DGDAL_USE_ARROW=OFF \
        -DGDAL_USE_PARQUET=OFF \
        -DGDAL_FIND_PACKAGE_PROJ_MODE=CONFIG \
        -DPROJ_DIR="$prefix/lib/cmake/proj" \
        -DZSTD_DIR="$prefix/lib/cmake/zstd" \
        -DCURL_INCLUDE_DIR="$sdk_root/usr/include" \
        -DCURL_LIBRARY="$system_curl_library" \
        -DSQLite3_INCLUDE_DIR="$sdk_root/usr/include" \
        -DSQLite3_LIBRARY="$system_sqlite_library" \
        -DGDAL_OBJECT_LIBRARIES_POSITION_INDEPENDENT_CODE=ON
    cmake --build "$component_build" --target install --parallel "$jobs"
}

verify_no_capability_leaks()
{
    local cache="$build_dir/gdal/CMakeCache.txt"
    local key
    local allowed
    [[ -f $cache ]] || die "missing GDAL CMakeCache.txt"

    while IFS= read -r key; do
        case "$key" in
            GDAL_ENABLE_DRIVER_GTIFF|GDAL_ENABLE_DRIVER_VRT|GDAL_ENABLE_DRIVER_MEM)
                ;;
            *)
                die "unrelated raster driver leaked into build: $key"
                ;;
        esac
    done < <(sed -n 's/^\(GDAL_ENABLE_DRIVER_[A-Z0-9_]*\):BOOL=ON$/\1/p' "$cache")

    while IFS= read -r key; do
        case "$key" in
            GDAL_USE_CURL|GDAL_USE_SQLITE3|GDAL_USE_ZSTD|\
            GDAL_USE_ZLIB_INTERNAL|GDAL_USE_TIFF_INTERNAL|\
            GDAL_USE_GEOTIFF_INTERNAL|GDAL_USE_JSONC_INTERNAL|\
            GDAL_USE_CPL_MULTIPROC_PTHREAD|GDAL_USE_SHAPELIB_INTERNAL)
                ;;
            *)
                die "unrequested external/internal capability leaked into build: $key"
                ;;
        esac
    done < <(sed -n 's/^\(GDAL_USE_[A-Z0-9_]*\):BOOL=ON$/\1/p' "$cache")

    allowed=$(sed -n 's/^\(OGR_ENABLE_DRIVER_[A-Z0-9_]*\):BOOL=ON$/\1/p' "$cache")
    [[ -z $allowed ]] || die "unrelated OGR driver leaked into build: $allowed"
}

verify_resolved_caches()
{
    local zstd_cache="$build_dir/zstd/CMakeCache.txt"
    local proj_cache="$build_dir/proj/CMakeCache.txt"
    local gdal_cache="$build_dir/gdal/CMakeCache.txt"
    [[ -f $zstd_cache && -f $proj_cache && -f $gdal_cache ]] ||
        die "resolved CMake caches are incomplete"

    verify_common_cache "$zstd_cache"
    cache_expect "$zstd_cache" ZSTD_BUILD_STATIC ON
    cache_expect "$zstd_cache" ZSTD_BUILD_SHARED OFF
    cache_expect "$zstd_cache" ZSTD_BUILD_PROGRAMS OFF
    cache_expect "$zstd_cache" ZSTD_BUILD_TESTS OFF

    verify_common_cache "$proj_cache"
    cache_expect "$proj_cache" BUILD_SHARED_LIBS OFF
    cache_expect "$proj_cache" BUILD_APPS OFF
    cache_expect "$proj_cache" BUILD_TESTING OFF
    cache_expect "$proj_cache" ENABLE_TIFF OFF
    cache_expect "$proj_cache" ENABLE_CURL OFF
    cache_expect "$proj_cache" EMBED_RESOURCE_FILES ON
    cache_expect "$proj_cache" USE_ONLY_EMBEDDED_RESOURCE_FILES ON

    verify_common_cache "$gdal_cache"
    cache_expect "$gdal_cache" BUILD_SHARED_LIBS OFF
    cache_expect "$gdal_cache" BUILD_APPS OFF
    cache_expect "$gdal_cache" BUILD_PYTHON_BINDINGS OFF
    cache_expect "$gdal_cache" CMAKE_DISABLE_FIND_PACKAGE_SWIG ON
    cache_expect "$gdal_cache" BUILD_TESTING OFF
    cache_expect "$gdal_cache" GDAL_BUILD_OPTIONAL_DRIVERS OFF
    cache_expect "$gdal_cache" OGR_BUILD_OPTIONAL_DRIVERS OFF
    cache_expect "$gdal_cache" GDAL_ENABLE_DRIVER_GTIFF ON
    cache_expect "$gdal_cache" GDAL_ENABLE_DRIVER_VRT ON
    cache_expect "$gdal_cache" GDAL_ENABLE_DRIVER_MEM ON
    cache_expect "$gdal_cache" OGR_ENABLE_DRIVER_GEOJSON OFF
    cache_expect "$gdal_cache" OGR_ENABLE_DRIVER_SHAPE OFF
    cache_expect "$gdal_cache" GDAL_USE_CURL ON
    cache_expect "$gdal_cache" GDAL_USE_SQLITE3 ON
    cache_expect "$gdal_cache" GDAL_USE_ZSTD ON
    cache_expect "$gdal_cache" GDAL_USE_ARROW OFF
    cache_expect "$gdal_cache" GDAL_USE_PARQUET OFF
    cache_expect "$gdal_cache" GDAL_USE_JPEG12_INTERNAL OFF
    cache_expect "$gdal_cache" ENABLE_DEFLATE64 OFF
    cache_expect "$gdal_cache" GDAL_OBJECT_LIBRARIES_POSITION_INDEPENDENT_CODE ON
    verify_no_capability_leaks
}

emit_manifest()
{
    verify_resolved_caches
    python3 - "$versions_file" "$build_dir" "$prefix" "$manifest" \
        "$system_curl_library" "$system_sqlite_library" <<'PY'
import json
import os
import re
import sys

versions_path, build_root, prefix, output, curl_library, sqlite_library = sys.argv[1:]

def read_env(path):
    values = {}
    with open(path, encoding="utf-8") as stream:
        for raw in stream:
            line = raw.strip()
            if line and not line.startswith("#"):
                key, value = line.split("=", 1)
                values[key] = value
    return values

def read_cache(component):
    values = {}
    types = {}
    path = os.path.join(build_root, component, "CMakeCache.txt")
    with open(path, encoding="utf-8", errors="replace") as stream:
        for raw in stream:
            match = re.match(r"^([^#/:][^:]*):([^=]+)=(.*)$", raw.rstrip("\n"))
            if match:
                key, value_type, value = match.groups()
                values[key] = value
                types[key] = value_type
    return values, types

versions = read_env(versions_path)
caches = {name: read_cache(name)[0] for name in ("zstd", "proj", "gdal")}
gdal = caches["gdal"]

drivers = sorted(
    key.removeprefix("GDAL_ENABLE_DRIVER_")
    for key, value in gdal.items()
    if key.startswith("GDAL_ENABLE_DRIVER_") and value == "ON"
)
ogr_drivers = sorted(
    key.removeprefix("OGR_ENABLE_DRIVER_")
    for key, value in gdal.items()
    if key.startswith("OGR_ENABLE_DRIVER_") and value == "ON"
)

selected_keys = {
    "zstd": [
        "CMAKE_BUILD_TYPE", "CMAKE_INSTALL_PREFIX", "CMAKE_OSX_DEPLOYMENT_TARGET",
        "CMAKE_POSITION_INDEPENDENT_CODE", "CMAKE_C_VISIBILITY_PRESET",
        "CMAKE_CXX_VISIBILITY_PRESET", "ZSTD_BUILD_STATIC", "ZSTD_BUILD_SHARED",
        "ZSTD_BUILD_PROGRAMS", "ZSTD_BUILD_TESTS", "ZSTD_BUILD_CONTRIB",
    ],
    "proj": [
        "CMAKE_BUILD_TYPE", "CMAKE_INSTALL_PREFIX", "CMAKE_OSX_DEPLOYMENT_TARGET",
        "CMAKE_POSITION_INDEPENDENT_CODE", "CMAKE_C_VISIBILITY_PRESET",
        "CMAKE_CXX_VISIBILITY_PRESET", "BUILD_SHARED_LIBS", "BUILD_APPS",
        "BUILD_TESTING", "BUILD_PROJSYNC", "ENABLE_TIFF", "ENABLE_CURL",
        "EMBED_PROJ_DATA_PATH", "EMBED_RESOURCE_FILES",
        "USE_ONLY_EMBEDDED_RESOURCE_FILES", "SQLite3_LIBRARY",
    ],
    "gdal": [
        "CMAKE_BUILD_TYPE", "CMAKE_INSTALL_PREFIX", "CMAKE_OSX_DEPLOYMENT_TARGET",
        "CMAKE_POSITION_INDEPENDENT_CODE", "CMAKE_C_VISIBILITY_PRESET",
        "CMAKE_CXX_VISIBILITY_PRESET", "BUILD_SHARED_LIBS", "BUILD_APPS",
        "BUILD_PYTHON_BINDINGS", "CMAKE_DISABLE_FIND_PACKAGE_SWIG",
        "BUILD_TESTING", "GDAL_BUILD_OPTIONAL_DRIVERS", "OGR_BUILD_OPTIONAL_DRIVERS",
        "GDAL_ENABLE_DRIVER_GTIFF", "GDAL_ENABLE_DRIVER_VRT",
        "GDAL_ENABLE_DRIVER_MEM", "OGR_ENABLE_DRIVER_GEOJSON",
        "OGR_ENABLE_DRIVER_SHAPE", "GDAL_USE_CURL", "GDAL_USE_SQLITE3",
        "GDAL_USE_ZSTD", "GDAL_USE_ARROW", "GDAL_USE_PARQUET",
        "GDAL_USE_ZLIB_INTERNAL", "GDAL_USE_JPEG12_INTERNAL",
        "ENABLE_DEFLATE64", "GDAL_USE_TIFF_INTERNAL",
        "GDAL_USE_GEOTIFF_INTERNAL", "GDAL_USE_JSONC_INTERNAL",
        "EMBED_RESOURCE_FILES", "USE_ONLY_EMBEDDED_RESOURCE_FILES",
        "GDAL_OBJECT_LIBRARIES_POSITION_INDEPENDENT_CODE", "CURL_LIBRARY",
        "SQLite3_LIBRARY", "PROJ_DIR", "ZSTD_DIR",
    ],
}

resolved = {}
for component, keys in selected_keys.items():
    missing = [key for key in keys if key not in caches[component]]
    if missing:
        raise SystemExit(f"missing resolved {component} cache keys: {', '.join(missing)}")
    resolved[component] = {key: caches[component][key] for key in keys}

prefix_files = []
for root, _, files in os.walk(prefix):
    for name in files:
        prefix_files.append(os.path.relpath(os.path.join(root, name), prefix))

document = {
    "schema_version": 1,
    "archives": {
        component: {
            "version": versions[f"{component.upper()}_VERSION"],
            "url": versions[f"{component.upper()}_URL"],
            "sha256": versions[f"{component.upper()}_SHA256"],
            "bytes": int(versions[f"{component.upper()}_ARCHIVE_BYTES"]),
        }
        for component in ("gdal", "proj", "zstd")
    },
    "resolved_cmake_cache": resolved,
    "compiled_raster_drivers": drivers,
    "compiled_ogr_drivers": ogr_drivers,
    "virtual_file_systems": ["/vsicurl/"] if gdal["GDAL_USE_CURL"] == "ON" else [],
    "features": {
        "curl": gdal["GDAL_USE_CURL"] == "ON",
        "sqlite3": gdal["GDAL_USE_SQLITE3"] == "ON",
        "proj": bool(gdal["PROJ_DIR"]),
        "zstd": gdal["GDAL_USE_ZSTD"] == "ON",
        "arrow": gdal["GDAL_USE_ARROW"] == "ON",
        "parquet": gdal["GDAL_USE_PARQUET"] == "ON",
        "proj_remote_grids": caches["proj"]["ENABLE_CURL"] == "ON",
        "apps": gdal["BUILD_APPS"] == "ON" or caches["proj"]["BUILD_APPS"] == "ON",
        "bindings": gdal["BUILD_PYTHON_BINDINGS"] == "ON" or
                    gdal["CMAKE_DISABLE_FIND_PACKAGE_SWIG"] != "ON",
        "tests": gdal["BUILD_TESTING"] == "ON" or caches["proj"]["BUILD_TESTING"] == "ON",
    },
    "platform_dependencies": {
        "curl": curl_library,
        "sqlite3": sqlite_library,
    },
    "prefix_files": sorted(prefix_files),
}

os.makedirs(os.path.dirname(output), exist_ok=True)
temporary = output + ".tmp"
with open(temporary, "w", encoding="utf-8") as stream:
    json.dump(document, stream, indent=2, sort_keys=True)
    stream.write("\n")
os.replace(temporary, output)
PY
}

verify_prefix()
{
    verify_archives
    verify_resolved_caches
    [[ -f "$prefix/lib/libzstd.a" ]] || die "private libzstd.a is missing"
    [[ -f "$prefix/lib/libproj.a" ]] || die "private libproj.a is missing"
    [[ -f "$prefix/lib/libgdal.a" ]] || die "private libgdal.a is missing"
    if find "$prefix" -type f \( -name '*.dylib' -o -name '*.so' \) -print -quit | grep -q .; then
        die "shared library leaked into the static prefix"
    fi
    emit_manifest
    python3 - "$manifest" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)
if manifest["compiled_raster_drivers"] != ["GTIFF", "MEM", "VRT"]:
    raise SystemExit(f"unexpected raster drivers: {manifest['compiled_raster_drivers']}")
if manifest["compiled_ogr_drivers"]:
    raise SystemExit(f"unexpected OGR drivers: {manifest['compiled_ogr_drivers']}")
expected = {
    "curl": True, "sqlite3": True, "proj": True, "zstd": True,
    "arrow": False, "parquet": False, "proj_remote_grids": False,
    "apps": False, "bindings": False, "tests": False,
}
if manifest["features"] != expected:
    raise SystemExit(f"unexpected features: {manifest['features']}")
if manifest["virtual_file_systems"] != ["/vsicurl/"]:
    raise SystemExit("/vsicurl/ is not resolved in the manifest")
print("[science-deps] verified private static prefix and manifest")
PY
}

case "$mode" in
    --download-only)
        download_all
        ;;
    --build)
        download_all
        extract_all
        start_seconds=$SECONDS
        rm -rf "$prefix"
        mkdir -p "$prefix"
        build_zstd
        build_proj
        build_gdal
        elapsed=$((SECONDS - start_seconds))
        printf '%s\n' "$elapsed" >"$science_root/build-duration-seconds.txt"
        verify_prefix
        ;;
    --verify)
        verify_prefix
        ;;
esac
