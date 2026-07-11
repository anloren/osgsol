#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
versions_file="$repo_root/packaging/science_deps/versions.env"
checksums_file="$repo_root/packaging/science_deps/checksums.txt"
builder="$repo_root/packaging/science_deps/build_science_deps.sh"
runtime_probe="$repo_root/packaging/science_deps/runtime_probe.cpp"
runtime_probe_cmake="$repo_root/packaging/science_deps/runtime_probe_CMakeLists.txt"
embed_probe="$repo_root/packaging/science_deps/gdal_embed_probe.c"
gdal_patch="$repo_root/packaging/science_deps/gdal-3.13.1-disable-shapelib.patch"
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
[[ -f "$runtime_probe" ]] || fail "missing compiled static-prefix runtime probe"
[[ -f "$runtime_probe_cmake" ]] || fail "missing runtime probe CMake project"
[[ -f "$embed_probe" ]] || fail "missing independent GDAL #embed probe"
[[ -f "$gdal_patch" ]] || fail "missing pinned GDAL Shapelib-disable patch"

# shellcheck disable=SC1090
source "$versions_file"
[[ ${GDAL_VERSION:-} == 3.13.1 ]] || fail "GDAL must be pinned to 3.13.1"
[[ ${PROJ_VERSION:-} == 9.8.1 ]] || fail "PROJ must be pinned to 9.8.1"
[[ ${ZSTD_VERSION:-} == 1.5.7 ]] || fail "ZSTD must be pinned to 1.5.7"

for variable in GDAL_SHA256 PROJ_SHA256 ZSTD_SHA256; do
    value=${!variable:-}
    [[ $value =~ ^[0-9a-f]{64}$ ]] || fail "$variable must contain a SHA-256 value"
done

for component in GDAL PROJ ZSTD; do
    archive_variable="${component}_ARCHIVE"
    sha_variable="${component}_SHA256"
    archive=${!archive_variable}
    expected_sha=${!sha_variable}
    checksum_sha=$(awk -v archive="$archive" '$2 == archive { print $1 }' "$checksums_file")
    [[ $checksum_sha == "$expected_sha" ]] ||
        fail "$component SHA differs between versions.env and checksums.txt"
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
cleanup_path_tests()
{
    find "$dangerous_root" -depth -delete
    if [[ -e $safe_candidate || -L $safe_candidate ]]; then
        find "$safe_candidate" -depth -delete
    fi
}
trap cleanup_path_tests EXIT
safe_candidate="$repo_root/build/science-deps-contract-$$"
canary="$dangerous_root/canary"
printf 'do not delete\n' >"$canary"

expect_unsafe_override()
{
    local label=$1
    shift
    if env "$@" "$builder" --verify >"$dangerous_root/stdout" \
        2>"$dangerous_root/stderr"; then
        fail "builder accepted dangerous $label override"
    fi
    grep -Eqi 'refus|forbid|unsafe|descendant|overlap|marker' "$dangerous_root/stderr" ||
        fail "builder did not reject dangerous $label before other work"
    [[ $(<"$canary") == 'do not delete' ]] ||
        fail "dangerous $label override deleted the canary"
}

expect_unsafe_override prefix-repo \
    SCIENCE_DEPS_ROOT="$safe_candidate" SCIENCE_DEPS_PREFIX="$repo_root"
expect_unsafe_override src-repo \
    SCIENCE_DEPS_ROOT="$safe_candidate" SCIENCE_DEPS_SRC="$repo_root"
expect_unsafe_override build-home \
    SCIENCE_DEPS_ROOT="$safe_candidate" SCIENCE_DEPS_BUILD="$HOME"
expect_unsafe_override downloads-system \
    SCIENCE_DEPS_ROOT="$safe_candidate" SCIENCE_DEPS_DOWNLOADS=/usr/local/science-downloads
expect_unsafe_override root-repo SCIENCE_DEPS_ROOT="$repo_root"
expect_unsafe_override root-home SCIENCE_DEPS_ROOT="$HOME"
expect_unsafe_override overlapping-children \
    SCIENCE_DEPS_ROOT="$safe_candidate" \
    SCIENCE_DEPS_SRC="$safe_candidate/build" \
    SCIENCE_DEPS_BUILD="$safe_candidate/build"

mkdir -p "$safe_candidate/real-prefix"
ln -s real-prefix "$safe_candidate/prefix-link"
if env SCIENCE_DEPS_ROOT="$safe_candidate" \
        SCIENCE_DEPS_PREFIX="$safe_candidate/prefix-link" \
        bash "$builder" --download-only >"$dangerous_root/symlink-stdout" \
        2>"$dangerous_root/symlink-stderr"; then
    fail "builder accepted a symlinked prefix override"
fi
grep -Eqi 'symlink' "$dangerous_root/symlink-stderr" ||
    fail "builder did not identify the symlinked prefix before directory adoption"

# These are the only GDAL runtime capabilities admitted by the private stack.
for token in \
    'GDAL_BUILD_OPTIONAL_DRIVERS=OFF' \
    'OGR_BUILD_OPTIONAL_DRIVERS=OFF' \
    'GDAL_ENABLE_DRIVER_GTIFF=ON' \
    'GDAL_ENABLE_DRIVER_VRT=ON' \
    'OGR_ENABLE_DRIVER_GEOJSON=OFF' \
    'OGR_ENABLE_DRIVER_SHAPE=OFF' \
    'ENABLE_GNM=OFF' \
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
assert_contains "$builder" 'safe_reset_dir' \
    "every recursive deletion must use the owned-directory reset guard"
assert_contains "$builder" 'science-deps-owned' \
    "mutable dependency directories must carry ownership markers"
assert_contains "$builder" 'runtime-probe.json' \
    "manifest claims must consume the compiled runtime probe"
assert_contains "$builder" 'build_runtime_probe' \
    "builder must compile and run the static-prefix runtime probe"
assert_contains "$runtime_probe_cmake" 'set\(zstd_DIR ' \
    "runtime probe must use zstd package's case-sensitive config directory variable"
assert_contains "$builder" 'verify_runtime_probe' \
    "verify must rerun and validate the static-prefix runtime probe"
assert_contains "$builder" 'runtime_probe.*active_drivers' \
    "manifest driver claims must come from runtime-probe output"
assert_contains "$builder" 'runtime_probe.*active_remote_vfs' \
    "manifest remote VFS claims must come from runtime-probe output"
assert_contains "$builder" 'gdal-embed-capability' \
    "GDAL embedding must be tied to an independent compiler probe"
assert_contains "$builder" 'cache_expect.*ENABLE_GNM.*OFF' \
    "verify must require GNM disabled in the resolved cache"
assert_contains "$builder" 'cache_expect.*GDAL_USE_SHAPELIB_INTERNAL.*OFF' \
    "verify must require internal Shapelib disabled"
assert_contains "$builder" 'EMBED_RESOURCE_FILES.*gdal_embed_supported' \
    "GDAL cache embedding must match the independent probe"
assert_contains "$builder" 'share/gdal' \
    "non-embedded GDAL builds must require installed resource data"
assert_contains "$builder" 'safe_reset_dir.*src_dir' \
    "every build must cleanly re-extract verified sources"
assert_contains "$builder" 'safe_reset_dir.*build_dir' \
    "every build must reset the owned build directory"
assert_contains "$builder" 'safe_reset_dir.*prefix' \
    "every build must reset the owned prefix"
assert_contains "$builder" 'realpath.*output' \
    "manifest inventory must exclude the manifest itself"
assert_contains "$builder" 'verify_static_artifacts' \
    "verify must inspect static archives and linked probe symbols"
assert_contains "$builder" 'verify_pin_contract' \
    "builder must tie versions.env pins to checksums.txt"
assert_not_contains "$runtime_probe" 'GDALAllRegister' \
    "runtime product contract must never use GDALAllRegister"
for symbol in GDALRegister_GTiff GDALRegister_VRT GDALRegister_MEM \
              VSIInstallCurlFileHandler GDALReprojectImage; do
    assert_contains "$runtime_probe" "$symbol" "runtime probe is missing $symbol"
done
assert_contains "$runtime_probe" 'COMPRESS=ZSTD' \
    "runtime probe must exercise GTiff ZSTD compression"
assert_contains "$runtime_probe" 'VSIFileManager::RemoveHandler' \
    "runtime probe must remove unwanted remote VFS handlers"
assert_contains "$runtime_probe" 'COG' \
    "runtime probe must reject active COG registration"
assert_contains "$runtime_probe" 'GNM' \
    "runtime probe must reject active GNM registration"
assert_contains "$tests_cmake" 'osgSol_Test_ScienceDepsScript' \
    "private dependency script contract must be registered with CTest"
assert_contains "$tests_cmake" 'science_deps_script_tests.sh' \
    "CTest registration must execute the dependency script contract"

echo "[OK] ScienceEarth private dependency builder contract"
