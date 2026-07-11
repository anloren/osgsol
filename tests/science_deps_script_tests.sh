#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
versions_file="$repo_root/packaging/science_deps/versions.env"
checksums_file="$repo_root/packaging/science_deps/checksums.txt"
builder="$repo_root/packaging/science_deps/build_science_deps.sh"
tests_cmake="$repo_root/tests/CMakeLists.txt"

fail()
{
    echo "[FAIL] $*" >&2
    exit 1
}

assert_contains()
{
    local file=$1
    local pattern=$2
    local description=$3
    grep -Eq -- "$pattern" "$file" || fail "$description"
}

assert_not_contains()
{
    local file=$1
    local pattern=$2
    local description=$3
    if grep -Eiq -- "$pattern" "$file"; then
        fail "$description"
    fi
}

[[ -f "$versions_file" ]] || fail "missing pinned versions file"
[[ -f "$checksums_file" ]] || fail "missing checksum file"
[[ -x "$builder" ]] || fail "missing executable dependency builder"

# shellcheck disable=SC1090
source "$versions_file"
[[ ${GDAL_VERSION:-} == 3.13.1 ]] || fail "GDAL must be pinned to 3.13.1"
[[ ${PROJ_VERSION:-} == 9.8.1 ]] || fail "PROJ must be pinned to 9.8.1"
[[ ${ZSTD_VERSION:-} == 1.5.7 ]] || fail "ZSTD must be pinned to 1.5.7"

for variable in GDAL_SHA256 PROJ_SHA256 ZSTD_SHA256; do
    value=${!variable:-}
    [[ $value =~ ^[0-9a-f]{64}$ ]] || fail "$variable must contain a SHA-256 value"
done

for archive in "gdal-${GDAL_VERSION}.tar.gz" "proj-${PROJ_VERSION}.tar.gz" \
               "zstd-${ZSTD_VERSION}.tar.gz"; do
    assert_contains "$checksums_file" "^[0-9a-f]{64}  ${archive}$" \
        "checksums.txt must pin $archive"
done

assert_contains "$builder" 'build/science-deps' \
    "builder must default below build/science-deps"
for directory in downloads src build prefix; do
    assert_contains "$builder" "${directory}" \
        "builder must define the default $directory directory"
done

assert_not_contains "$builder" 'brew[[:space:]]+install|sudo([[:space:]]|$)' \
    "builder must never install with Homebrew or sudo"

dangerous_root=$(mktemp -d "${TMPDIR:-/tmp}/science-deps-contract.XXXXXX")
trap 'rm -rf "$dangerous_root"' EXIT
for prefix in /opt/homebrew/science-deps /usr/local/science-deps; do
    if SCIENCE_DEPS_ROOT="$dangerous_root/root" SCIENCE_DEPS_PREFIX="$prefix" \
        "$builder" --verify >"$dangerous_root/stdout" 2>"$dangerous_root/stderr"; then
        fail "builder accepted forbidden prefix $prefix"
    fi
    grep -qi 'refus\|forbid\|unsafe' "$dangerous_root/stderr" ||
        fail "builder did not explain why $prefix is forbidden"
done

# These are the only GDAL runtime capabilities admitted by the private stack.
for token in \
    'GDAL_BUILD_OPTIONAL_DRIVERS=OFF' \
    'OGR_BUILD_OPTIONAL_DRIVERS=OFF' \
    'GDAL_ENABLE_DRIVER_GTIFF=ON' \
    'GDAL_ENABLE_DRIVER_VRT=ON' \
    'OGR_ENABLE_DRIVER_GEOJSON=OFF' \
    'OGR_ENABLE_DRIVER_SHAPE=OFF' \
    'GDAL_USE_CURL=ON' \
    'GDAL_USE_SQLITE3=ON' \
    'GDAL_USE_ZSTD=ON' \
    'GDAL_USE_ZLIB=OFF' \
    'GDAL_USE_JPEG12_INTERNAL=OFF' \
    'ENABLE_DEFLATE64=OFF' \
    'GDAL_USE_ARROW=OFF' \
    'GDAL_USE_PARQUET=OFF' \
    'BUILD_APPS=OFF' \
    'BUILD_PYTHON_BINDINGS=OFF' \
    'CMAKE_DISABLE_FIND_PACKAGE_SWIG=ON' \
    'BUILD_TESTING=OFF'; do
    assert_contains "$builder" "$token" "builder is missing resolved capability $token"
done

assert_contains "$builder" 'builtin_raster_drivers=\(MEM\)' \
    "builder must account for the always-built MEM driver without inventing a CMake option"

assert_contains "$builder" 'CMAKE_POSITION_INDEPENDENT_CODE=ON' \
    "static dependencies must be position independent"
assert_contains "$builder" 'CMAKE_C_VISIBILITY_PRESET=hidden' \
    "C symbols must use hidden visibility"
assert_contains "$builder" 'CMAKE_CXX_VISIBILITY_PRESET=hidden' \
    "C++ symbols must use hidden visibility"
assert_contains "$builder" 'cache_expect.*CMAKE_POSITION_INDEPENDENT_CODE' \
    "verify must reject a cache that loses position-independent code"
assert_contains "$builder" 'cache_expect.*CMAKE_C_VISIBILITY_PRESET' \
    "verify must reject a cache that loses hidden C visibility"
assert_contains "$builder" 'cache_expect.*CMAKE_OSX_DEPLOYMENT_TARGET' \
    "verify must reject a cache with a mismatched deployment target"
assert_contains "$builder" 'ENABLE_CURL=OFF' \
    "PROJ remote-grid transport must be disabled with its accepted CMake option"
assert_not_contains "$builder" 'GDAL_ENABLE_DRIVER_MEM=|PROJ_NETWORK=' \
    "builder must not pass invented CMake options"
assert_not_contains "$builder" 'DBUILD_(JAVA|CSHARP)_BINDINGS=' \
    "builder must not pass binding options that are conditional on discovered tools"
[[ $(grep -c -- '-DEMBED_RESOURCE_FILES=ON' "$builder") -eq 1 ]] ||
    fail "only PROJ may force resource embedding; GDAL must use its compiler probe"
assert_contains "$builder" 'Manually-specified variables were not used' \
    "builder must fail when CMake reports an unrecognized option"
assert_contains "$builder" 'zstd-\$ZSTD_VERSION/build/cmake/CMakeLists.txt' \
    "ZSTD extraction must validate its actual nested CMake entry point"
assert_contains "$builder" 'CMakeCache.txt' \
    "manifest must be derived from resolved CMake caches"
assert_contains "$builder" 'science-deps-manifest.json' \
    "builder must emit science-deps-manifest.json"
assert_contains "$tests_cmake" 'osgSol_Test_ScienceDepsScript' \
    "private dependency script contract must be registered with CTest"
assert_contains "$tests_cmake" 'science_deps_script_tests.sh' \
    "CTest registration must execute the dependency script contract"

echo "[OK] ScienceEarth private dependency builder contract"
