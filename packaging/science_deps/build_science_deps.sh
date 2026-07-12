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
runtime_probe_json="$build_dir/runtime-probe.json"
runtime_probe_links="$build_dir/runtime-probe-link-dependencies.txt"
inactive_compiled_helpers="$build_dir/inactive-compiled-helpers.json"
gdal_embed_capability="$build_dir/gdal-embed-capability.txt"
deployment_target=${SCIENCE_DEPS_DEPLOYMENT_TARGET:-11.0}
builtin_raster_drivers=(MEM)
marker_name=.science-deps-owned
marker_value="osgsol-science-deps-v2:$repo_root"

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

raw_science_root=$science_root
raw_downloads_dir=$downloads_dir
raw_src_dir=$src_dir
raw_build_dir=$build_dir
raw_prefix=$prefix

python3 - "$raw_science_root" "$raw_downloads_dir" "$raw_src_dir" \
    "$raw_build_dir" "$raw_prefix" <<'PY' || exit 1
import os
import sys

labels = ("ROOT", "DOWNLOADS", "SRC", "BUILD", "PREFIX")
for label, raw in zip(labels, sys.argv[1:]):
    path = os.path.abspath(raw)
    while True:
        if os.path.islink(path):
            raise SystemExit(
                f"[science-deps] error: refusing symlinked {label} path component: {path}"
            )
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
PY

canonical_path()
{
    python3 - "$1" <<'PY'
import os
import sys
print(os.path.realpath(sys.argv[1]))
PY
}

repo_build=$(canonical_path "$repo_root/build")
science_root=$(canonical_path "$science_root")
downloads_dir=$(canonical_path "$downloads_dir")
src_dir=$(canonical_path "$src_dir")
build_dir=$(canonical_path "$build_dir")
prefix=$(canonical_path "$prefix")
manifest="$prefix/science-deps-manifest.json"

python3 - "$repo_build" "$science_root" "$downloads_dir" "$src_dir" \
    "$build_dir" "$prefix" <<'PY' || exit 1
import os
import sys

repo_build, root, downloads, source, build, prefix = sys.argv[1:]
children = {
    "DOWNLOADS": downloads,
    "SRC": source,
    "BUILD": build,
    "PREFIX": prefix,
}

def descendant(path, parent):
    try:
        return path != parent and os.path.commonpath((path, parent)) == parent
    except ValueError:
        return False

if not descendant(root, repo_build):
    raise SystemExit(
        f"[science-deps] error: refusing unsafe ROOT; it must be a strict descendant of {repo_build}"
    )

for label, path in children.items():
    if not descendant(path, root):
        raise SystemExit(
            f"[science-deps] error: refusing unsafe {label}; it must be a strict ROOT descendant"
        )

items = list(children.items())
for index, (left_label, left) in enumerate(items):
    for right_label, right in items[index + 1:]:
        if left == right or descendant(left, right) or descendant(right, left):
            raise SystemExit(
                f"[science-deps] error: refusing overlapping {left_label}/{right_label} paths"
            )
PY

for tool in ar cc cmake curl file nm otool patch python3 shasum strings tar; do
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

marker_path()
{
    printf '%s/%s\n' "$1" "$marker_name"
}

marker_matches()
{
    local marker
    marker=$(marker_path "$1")
    [[ -f $marker && $(<"$marker") == "$marker_value" ]]
}

write_marker()
{
    printf '%s\n' "$marker_value" >"$(marker_path "$1")"
}

prepare_owned_dir()
{
    local path=$1
    [[ ! -L $path ]] || die "refusing symlinked owned directory: $path"
    if [[ -d $path ]]; then
        if marker_matches "$path"; then
            return
        fi
        if [[ -n $(find "$path" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
            die "refusing unmarked non-empty override directory: $path"
        fi
    else
        mkdir -p "$path"
    fi
    write_marker "$path"
}

assert_owned_descendant()
{
    local path=$1
    local canonical
    canonical=$(canonical_path "$path")
    [[ $canonical == "$path" && $path == "$science_root"/* ]] ||
        die "refusing recursive delete outside canonical ROOT descendant: $path"
    [[ ! -L $path ]] || die "refusing recursive delete of symlink: $path"
    marker_matches "$path" || die "refusing recursive delete without ownership marker: $path"
}

safe_reset_dir()
{
    local path=$1
    assert_owned_descendant "$path"
    rm -rf -- "$path"
    mkdir -p "$path"
    write_marker "$path"
}

prepare_owned_dir "$science_root"
prepare_owned_dir "$downloads_dir"
prepare_owned_dir "$src_dir"
prepare_owned_dir "$build_dir"
prepare_owned_dir "$prefix"

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

verify_pin_contract()
{
    local line_count
    line_count=$(awk 'NF { count++ } END { print count + 0 }' "$checksums_file")
    [[ $line_count == 3 ]] || die "checksums.txt must contain exactly three archive pins"

    local component archive_variable sha_variable archive expected actual matches
    for component in GDAL PROJ ZSTD; do
        archive_variable="${component}_ARCHIVE"
        sha_variable="${component}_SHA256"
        archive=${!archive_variable}
        expected=${!sha_variable}
        actual=$(awk -v archive="$archive" '$2 == archive { print $1 }' "$checksums_file")
        matches=$(awk -v archive="$archive" '$2 == archive { count++ } END { print count + 0 }' \
            "$checksums_file")
        [[ $matches == 1 && $actual == "$expected" ]] ||
            die "$component pin differs between versions.env and checksums.txt"
    done

    actual=$(shasum -a 256 "$script_dir/gdal-3.13.1-disable-shapelib.patch" | awk '{print $1}')
    [[ $actual == "$GDAL_PATCH_SHA256" ]] || die "GDAL source patch checksum mismatch"
    actual=$(shasum -a 256 \
        "$script_dir/gdal-3.13.1-relocatable-static.patch" | awk '{print $1}')
    [[ $actual == "$GDAL_RELOCATABLE_PATCH_SHA256" ]] ||
        die "GDAL relocatable source patch checksum mismatch"
    actual=$(shasum -a 256 \
        "$script_dir/gdal-3.13.1-parallel-head-range.patch" | awk '{print $1}')
    [[ $actual == "$GDAL_PREFETCH_PATCH_SHA256" ]] ||
        die "GDAL parallel HEAD/Range patch checksum mismatch"
}

verify_archives()
{
    verify_pin_contract
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
    local sentinel=$2
    tar -xzf "$downloads_dir/$archive" -C "$src_dir"
    [[ -f "$src_dir/$sentinel" ]] || die "archive did not extract $sentinel"
}

extract_all()
{
    local gdal_cmake_entry="gdal-$GDAL_VERSION/CMakeLists.txt"
    local proj_cmake_entry="proj-$PROJ_VERSION/CMakeLists.txt"
    local zstd_cmake_entry="zstd-$ZSTD_VERSION/build/cmake/CMakeLists.txt"
    extract_archive "$GDAL_ARCHIVE" "$gdal_cmake_entry"
    extract_archive "$PROJ_ARCHIVE" "$proj_cmake_entry"
    extract_archive "$ZSTD_ARCHIVE" "$zstd_cmake_entry"
    (
        cd "$src_dir/gdal-$GDAL_VERSION"
        patch --batch --forward -p1 <"$script_dir/gdal-3.13.1-disable-shapelib.patch"
        patch --batch --forward -p1 <"$script_dir/gdal-3.13.1-relocatable-static.patch"
        patch --batch --forward -p1 <"$script_dir/gdal-3.13.1-parallel-head-range.patch"
    )
}

probe_gdal_embed_capability()
{
    local object="$build_dir/gdal-embed-probe.o"
    local log="$build_dir/gdal-embed-probe.log"
    local result=OFF
    if MACOSX_DEPLOYMENT_TARGET="$deployment_target" cc -std=gnu2x \
        -c "$script_dir/gdal_embed_probe.c" -o "$object" >"$log" 2>&1; then
        result=ON
    fi
    printf '%s\n' "$result" >"$gdal_embed_capability"
    echo "[science-deps] independent GDAL #embed capability: $result"
}

load_gdal_embed_capability()
{
    [[ -f $gdal_embed_capability ]] || die "missing independent GDAL #embed result"
    gdal_embed_supported=$(<"$gdal_embed_capability")
    [[ $gdal_embed_supported == ON || $gdal_embed_supported == OFF ]] ||
        die "invalid independent GDAL #embed result"
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

source_map="-ffile-prefix-map=${science_root}=ScienceEarthDeps"
common_cmake_args=(
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_C_FLAGS=${source_map}"
    "-DCMAKE_CXX_FLAGS=${source_map}"
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
    load_gdal_embed_capability
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
        -DENABLE_GNM=OFF \
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
        -D_TEST_SHARP_EMBED="$gdal_embed_supported" \
        -DEMBED_RESOURCE_FILES="$gdal_embed_supported" \
        -DUSE_ONLY_EMBEDDED_RESOURCE_FILES="$gdal_embed_supported" \
        -DGDAL_OBJECT_LIBRARIES_POSITION_INDEPENDENT_CODE=ON
    cmake --build "$component_build" --target install --parallel "$jobs"
}

build_runtime_probe()
{
    local source_dir="$build_dir/runtime-probe-source"
    local component_build="$build_dir/runtime-probe"
    mkdir -p "$source_dir"
    cmake -E copy "$script_dir/runtime_probe_CMakeLists.txt" "$source_dir/CMakeLists.txt"
    cmake -E copy "$script_dir/runtime_probe.cpp" "$source_dir/runtime_probe.cpp"
    configure_component runtime-probe \
        -S "$source_dir" \
        -B "$component_build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_VISIBILITY_PRESET=hidden \
        -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
        -DSCIENCE_DEPS_PREFIX="$prefix" \
        -DSCIENCE_DEPS_SDK="$sdk_root"
    cmake --build "$component_build" --parallel "$jobs"
    verify_runtime_probe
}

verify_runtime_probe()
{
    local executable="$build_dir/runtime-probe/science_deps_runtime_probe"
    local temporary="$runtime_probe_json.tmp"
    [[ -x $executable ]] || die "compiled static-prefix runtime probe is missing"
    "$executable" >"$temporary"
    python3 - "$temporary" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    probe = json.load(stream)
if probe["active_raster_drivers"] != ["GTiff", "MEM", "VRT"]:
    raise SystemExit(
        f"unexpected active raster drivers: {probe['active_raster_drivers']}"
    )
if probe["active_ogr_drivers"] != ["MEM"]:
    raise SystemExit(f"unexpected active OGR drivers: {probe['active_ogr_drivers']}")
if probe["active_remote_vfs"] != ["/vsicurl/"]:
    raise SystemExit(f"unexpected active remote VFS: {probe['active_remote_vfs']}")
for key in ("gtiff_zstd", "vrt_read", "mem_rasterio", "warp_proj"):
    if probe.get(key) is not True:
        raise SystemExit(f"runtime capability failed: {key}")
if probe.get("cog_active") is not False or probe.get("gnm_active") is not False:
    raise SystemExit("COG or GNM became active")
PY
    mv "$temporary" "$runtime_probe_json"
    otool -L "$executable" >"$runtime_probe_links"
}

verify_static_artifacts()
{
    local archive
    for archive in "$prefix/lib/libzstd.a" "$prefix/lib/libproj.a" "$prefix/lib/libgdal.a"; do
        [[ -f $archive ]] || die "private static archive is missing: $archive"
        file "$archive" | grep -Eq 'ar archive|current ar archive' ||
            die "installed library is not a static archive: $archive"
    done

    local executable="$build_dir/runtime-probe/science_deps_runtime_probe"
    local symbols="$build_dir/runtime-probe-symbols.txt"
    local archive_symbols="$build_dir/libgdal-archive-symbols.txt"
    local archive_members="$build_dir/libgdal-archive-members.txt"
    nm -gU "$executable" >"$symbols"
    for symbol in GDALRegister_GTiff GDALRegister_VRT GDALRegister_MEM; do
        grep -q "_$symbol" "$symbols" || die "runtime probe did not link $symbol"
    done
    if grep -Eq '_GDALAllRegister' "$symbols"; then
        die "broad driver registration leaked into runtime probe"
    fi
    if ar -t "$prefix/lib/libgdal.a" | grep -Eiq '(^|/)gnm'; then
        die "GNM objects leaked into libgdal.a"
    fi
    nm -gU "$prefix/lib/libgdal.a" >"$archive_symbols"
    ar -t "$prefix/lib/libgdal.a" >"$archive_members"
    python3 - "$archive_symbols" "$archive_members" "$inactive_compiled_helpers" <<'PY'
import json
import re
import sys

symbols_path, members_path, output_path = sys.argv[1:]
with open(symbols_path, encoding="utf-8", errors="replace") as stream:
    symbols = sorted(set(re.findall(r"\b_([A-Za-z][A-Za-z0-9_]*)$", stream.read(), re.M)))
with open(members_path, encoding="utf-8", errors="replace") as stream:
    members = sorted(set(line.strip() for line in stream if line.strip()))

required = {
    "GDALRegister_COG",
    "VSIInstallS3FileHandler",
    "VSIInstallGSFileHandler",
    "VSIInstallAzureFileHandler",
    "VSIInstallOSSFileHandler",
    "VSIInstallSwiftFileHandler",
}
missing = sorted(required.difference(symbols))
if missing:
    raise SystemExit(f"expected inactive compiled helper symbols are missing: {missing}")
if "cogdriver.cpp.o" not in members:
    raise SystemExit("expected inactive COG helper object is missing")

cloud_pattern = re.compile(
    r"^VSIInstall(?:ADLS|Azure|GS|OSS|S3|Swift)(?:Streaming)?FileHandler$"
)
cloud_installers = [symbol for symbol in symbols if cloud_pattern.match(symbol)]
cloud_objects = [
    member for member in members
    if member in {
        "cpl_azure.cpp.o", "cpl_vsil_gs.cpp.o", "cpl_vsil_oss.cpp.o",
        "cpl_vsil_s3.cpp.o", "cpl_vsil_swift.cpp.o",
    }
]
evidence = {
    "archive": "lib/libgdal.a",
    "runtime_status": "compiled_but_inactive",
    "cog_driver": {
        "objects": ["cogdriver.cpp.o"],
        "verified_symbols": ["GDALRegister_COG"],
        "note": (
            "GTiff includes COG registration and helper code; COG is not manually "
            "registered and the runtime probe requires it to remain inactive."
        ),
    },
    "cloud_vfs": {
        "objects": cloud_objects,
        "verified_installer_symbols": cloud_installers,
        "note": (
            "GDAL curl support compiles cloud VFS installers into the archive; the "
            "runtime probe removes them and requires /vsicurl/ as the only active "
            "remote VFS."
        ),
    },
}
with open(output_path, "w", encoding="utf-8") as stream:
    json.dump(evidence, stream, indent=2, sort_keys=True)
    stream.write("\n")
PY

    python3 - "$runtime_probe_links" <<'PY'
import sys

dependencies = []
with open(sys.argv[1], encoding="utf-8") as stream:
    next(stream, None)
    for raw in stream:
        line = raw.strip()
        if line:
            dependencies.append(line.split()[0])
for dependency in dependencies:
    if not (dependency.startswith("/usr/lib/") or
            dependency.startswith("/System/Library/Frameworks/")):
        raise SystemExit(f"unapproved runtime link dependency: {dependency}")
if not any("libcurl" in dependency for dependency in dependencies):
    raise SystemExit("runtime probe is not linked to macOS curl")
if not any("libsqlite3" in dependency for dependency in dependencies):
    raise SystemExit("runtime probe is not linked to macOS SQLite")
PY

    if grep -E '^[A-Za-z0-9_]*(LIBRARY|INCLUDE_DIR|_DIR):(FILEPATH|PATH)=/(opt/homebrew|usr/local)' \
        "$build_dir/zstd/CMakeCache.txt" "$build_dir/proj/CMakeCache.txt" \
        "$build_dir/gdal/CMakeCache.txt" "$build_dir/runtime-probe/CMakeCache.txt"; then
        die "Homebrew or /usr/local library/include/package path leaked into a resolved cache"
    fi
    if find "$prefix" -type f \( -name '*.dylib' -o -name '*.so' \) -print -quit | grep -q .; then
        die "shared library leaked into the static prefix"
    fi
}

verify_no_workspace_strings()
{
    local artifact
    for artifact in \
        "$build_dir/runtime-probe/science_deps_runtime_probe" \
        "$prefix/lib/libgdal.a" "$prefix/lib/libproj.a" "$prefix/lib/libzstd.a"; do
        if strings -a "$artifact" | grep -F "$science_root"; then
            die "workspace path leaked into runtime artifact: $artifact"
        fi
    done
}

verify_gdal_pkgconfig_metadata()
{
    local metadata="$prefix/lib/pkgconfig/gdal.pc"
    [[ -f $metadata ]] || die "installed GDAL pkg-config metadata is missing"
    python3 - "$metadata" "$prefix" <<'PY'
import os
import re
import sys

metadata_path, expected_prefix = sys.argv[1:]
values = {}
with open(metadata_path, encoding="utf-8") as stream:
    for raw in stream:
        match = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)=(.*)$", raw.rstrip("\n"))
        if match:
            values[match.group(1)] = match.group(2)

actual_prefix = values.get("CONFIG_INST_PREFIX")
if actual_prefix != expected_prefix:
    raise SystemExit(
        f"GDAL CONFIG_INST_PREFIX is {actual_prefix!r}; expected {expected_prefix!r}"
    )

variable_pattern = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")

def resolve(name, stack=()):
    if name in stack:
        raise SystemExit(f"recursive GDAL pkg-config variable: {name}")
    if name not in values:
        raise SystemExit(f"missing GDAL pkg-config variable: {name}")
    return variable_pattern.sub(
        lambda match: resolve(match.group(1), stack + (name,)), values[name]
    )

for name, suffix in (("libdir", "lib"), ("includedir", "include")):
    actual = os.path.normpath(resolve(name))
    expected = os.path.join(expected_prefix, suffix)
    if actual != expected:
        raise SystemExit(f"GDAL {name} resolves to {actual!r}; expected {expected!r}")
PY
}

verify_embed_contract()
{
    probe_gdal_embed_capability
    load_gdal_embed_capability
    local cache="$build_dir/gdal/CMakeCache.txt"
    cache_expect "$cache" EMBED_RESOURCE_FILES "$gdal_embed_supported"
    cache_expect "$cache" USE_ONLY_EMBEDDED_RESOURCE_FILES "$gdal_embed_supported"
    if [[ $gdal_embed_supported == OFF ]]; then
        [[ -d "$prefix/share/gdal" ]] || die "non-embedded GDAL resource directory is missing"
        find "$prefix/share/gdal" -type f -print -quit | grep -q . ||
            die "non-embedded GDAL resources were not installed"
    fi
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
            GDAL_USE_CPL_MULTIPROC_PTHREAD)
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
    cache_expect "$gdal_cache" ENABLE_GNM OFF
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
    cache_expect "$gdal_cache" GDAL_USE_SHAPELIB_INTERNAL OFF
    cache_expect "$gdal_cache" ENABLE_DEFLATE64 OFF
    cache_expect "$gdal_cache" GDAL_OBJECT_LIBRARIES_POSITION_INDEPENDENT_CODE ON
    verify_no_capability_leaks
}

emit_manifest()
{
    verify_resolved_caches
    [[ -f $runtime_probe_json && -f $runtime_probe_links && \
       -f $inactive_compiled_helpers ]] ||
        die "runtime probe evidence is missing"
    python3 - "$versions_file" "$build_dir" "$prefix" "$manifest" \
        "$system_curl_library" "$system_sqlite_library" "$runtime_probe_json" \
        "$runtime_probe_links" "$gdal_embed_capability" \
        "$inactive_compiled_helpers" <<'PY'
import json
import os
import re
import sys

(versions_path, build_root, prefix, output, curl_library, sqlite_library,
 runtime_probe_path, runtime_links_path, embed_capability_path,
 inactive_helpers_path) = sys.argv[1:]

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
with open(runtime_probe_path, encoding="utf-8") as stream:
    runtime_probe = json.load(stream)
with open(embed_capability_path, encoding="utf-8") as stream:
    embed_capability = stream.read().strip()
with open(inactive_helpers_path, encoding="utf-8") as stream:
    inactive_helpers = json.load(stream)

link_dependencies = []
with open(runtime_links_path, encoding="utf-8") as stream:
    next(stream, None)
    for raw in stream:
        line = raw.strip()
        if line:
            link_dependencies.append(line.split()[0])

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
        "BUILD_TESTING", "ENABLE_GNM", "GDAL_BUILD_OPTIONAL_DRIVERS",
        "OGR_BUILD_OPTIONAL_DRIVERS",
        "GDAL_ENABLE_DRIVER_GTIFF", "GDAL_ENABLE_DRIVER_VRT",
        "GDAL_ENABLE_DRIVER_MEM", "OGR_ENABLE_DRIVER_GEOJSON",
        "OGR_ENABLE_DRIVER_SHAPE", "GDAL_USE_CURL", "GDAL_USE_SQLITE3",
        "GDAL_USE_ZSTD", "GDAL_USE_ARROW", "GDAL_USE_PARQUET",
        "GDAL_USE_ZLIB_INTERNAL", "GDAL_USE_JPEG12_INTERNAL",
        "GDAL_USE_SHAPELIB_INTERNAL",
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
output_real = os.path.realpath(output)
temporary_real = os.path.realpath(output + ".tmp")
for root, _, files in os.walk(prefix):
    for name in files:
        full_path = os.path.join(root, name)
        if os.path.realpath(full_path) in (output_real, temporary_real):
            continue
        if name == ".science-deps-owned":
            continue
        prefix_files.append(os.path.relpath(full_path, prefix))

document = {
    "schema_version": 3,
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
    "active_raster_drivers": runtime_probe["active_raster_drivers"],
    "active_ogr_drivers": runtime_probe["active_ogr_drivers"],
    "active_remote_vfs": runtime_probe["active_remote_vfs"],
    "inactive_compiled_helpers": inactive_helpers,
    "runtime_probe": runtime_probe,
    "runtime_link_dependencies": link_dependencies,
    "gdal_embed_probe": {
        "supported": embed_capability == "ON",
        "resolved_cache": gdal["EMBED_RESOURCE_FILES"] == "ON",
    },
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
    verify_embed_contract
    verify_runtime_probe
    verify_static_artifacts
    verify_no_workspace_strings
    verify_gdal_pkgconfig_metadata
    emit_manifest
    local first_manifest_sha second_manifest_sha
    first_manifest_sha=$(shasum -a 256 "$manifest" | awk '{print $1}')
    emit_manifest
    second_manifest_sha=$(shasum -a 256 "$manifest" | awk '{print $1}')
    [[ $first_manifest_sha == "$second_manifest_sha" ]] ||
        die "science dependency manifest is not stable across repeated emission"
    python3 - "$manifest" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)
if manifest["schema_version"] != 3:
    raise SystemExit(f"unexpected manifest schema: {manifest['schema_version']}")
if manifest["active_raster_drivers"] != ["GTiff", "MEM", "VRT"]:
    raise SystemExit(f"unexpected raster drivers: {manifest['active_raster_drivers']}")
if manifest["active_ogr_drivers"] != ["MEM"]:
    raise SystemExit(f"unexpected OGR drivers: {manifest['active_ogr_drivers']}")
expected = {
    "curl": True, "sqlite3": True, "proj": True, "zstd": True,
    "arrow": False, "parquet": False, "proj_remote_grids": False,
    "apps": False, "bindings": False, "tests": False,
}
if manifest["features"] != expected:
    raise SystemExit(f"unexpected features: {manifest['features']}")
if manifest["active_remote_vfs"] != ["/vsicurl/"]:
    raise SystemExit("/vsicurl/ is not resolved in the manifest")
if "science-deps-manifest.json" in manifest["prefix_files"]:
    raise SystemExit("manifest includes itself in prefix inventory")
probe = manifest["runtime_probe"]
if probe["schema_version"] != 2:
    raise SystemExit(f"unexpected runtime-probe schema: {probe['schema_version']}")
if probe["active_raster_drivers"] != ["GTiff", "MEM", "VRT"]:
    raise SystemExit("manifest did not preserve runtime-proven drivers")
if probe["active_ogr_drivers"] != ["MEM"]:
    raise SystemExit("manifest did not preserve runtime-proven OGR driver set")
if probe["active_remote_vfs"] != ["/vsicurl/"]:
    raise SystemExit("manifest did not preserve runtime-proven remote VFS")
if manifest["gdal_embed_probe"]["supported"] != \
        manifest["gdal_embed_probe"]["resolved_cache"]:
    raise SystemExit("GDAL embed probe and resolved cache disagree")
helpers = manifest["inactive_compiled_helpers"]
if helpers["runtime_status"] != "compiled_but_inactive":
    raise SystemExit("inactive compiled helper status is missing")
if helpers["cog_driver"]["verified_symbols"] != ["GDALRegister_COG"]:
    raise SystemExit("inactive compiled COG surface is not disclosed")
required_cloud = {
    "VSIInstallS3FileHandler", "VSIInstallGSFileHandler",
    "VSIInstallAzureFileHandler", "VSIInstallOSSFileHandler",
    "VSIInstallSwiftFileHandler",
}
if not required_cloud.issubset(helpers["cloud_vfs"]["verified_installer_symbols"]):
    raise SystemExit("inactive compiled cloud VFS surface is not disclosed")
print("[science-deps] verified private static prefix and manifest")
PY
}

case "$mode" in
    --download-only)
        download_all
        ;;
    --build)
        download_all
        safe_reset_dir "$src_dir"
        safe_reset_dir "$build_dir"
        safe_reset_dir "$prefix"
        extract_all
        probe_gdal_embed_capability
        start_seconds=$SECONDS
        build_zstd
        build_proj
        build_gdal
        build_runtime_probe
        elapsed=$((SECONDS - start_seconds))
        printf '%s\n' "$elapsed" >"$science_root/build-duration-seconds.txt"
        verify_prefix
        ;;
    --verify)
        verify_prefix
        ;;
esac
