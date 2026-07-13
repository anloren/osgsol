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
relocatable_patch="$repo_root/packaging/science_deps/gdal-3.13.1-relocatable-static.patch"
prefetch_patch="$repo_root/packaging/science_deps/gdal-3.13.1-parallel-head-range.patch"
prefetch_trace="$repo_root/tests/data/science/prefetch_nvidia_partial_trace.log"
prefetch_stats="$repo_root/tests/data/science/prefetch_nvidia_partial_stats.json"
prefetch_transient_trace="$repo_root/tests/data/science/prefetch_hong_kong_transient_trace.log"
prefetch_transient_stats="$repo_root/tests/data/science/prefetch_hong_kong_transient_stats.json"
prefetch_retry_trace="$repo_root/tests/data/science/prefetch_nvidia_transient_retry_trace.log"
prefetch_retry_stats="$repo_root/tests/data/science/prefetch_nvidia_transient_retry_stats.json"
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
[[ -f "$relocatable_patch" ]] || fail "missing pinned relocatable GDAL patch"
[[ -f "$prefetch_patch" ]] || fail "missing pinned parallel HEAD/Range GDAL patch"
[[ -f "$prefetch_trace" ]] || fail "missing credential-free prefetch replay trace"
[[ -f "$prefetch_stats" ]] || fail "missing prefetch replay network statistics"
[[ -f "$prefetch_transient_trace" ]] ||
    fail "missing credential-free transient fallback replay trace"
[[ -f "$prefetch_transient_stats" ]] ||
    fail "missing transient fallback replay network statistics"
[[ -f "$prefetch_retry_trace" ]] ||
    fail "missing credential-free transient retry replay trace"
[[ -f "$prefetch_retry_stats" ]] ||
    fail "missing transient retry replay network statistics"
if grep -nE '[[:blank:]]$' "$prefetch_patch" >/dev/null; then
    fail "parallel HEAD/Range patch must not contain nested trailing whitespace"
fi
assert_not_contains "$relocatable_patch" '^--- a/cmake/helpers/configure\.cmake' \
    "embedded-only GDAL patch must preserve GDAL_PREFIX for installed package metadata"
assert_contains "$relocatable_patch" '^\+configure_file\($' \
    "embedded-only GDAL patch must narrow prefix suppression to cpl_config.h generation"
assert_contains "$relocatable_patch" '^\+  \$\{GDAL_CMAKE_TEMPLATE_PATH\}/cpl_config\.h\.in$' \
    "embedded-only GDAL patch must configure cpl_config.h while the prefix is hidden"
assert_contains "$relocatable_patch" \
    'set\(GDAL_PREFIX "\$\{_gdal_prefix_for_package_metadata\}"\)' \
    "embedded-only GDAL patch must restore the prefix used by package metadata"

# shellcheck disable=SC1090
source "$versions_file"
[[ ${GDAL_VERSION:-} == 3.13.1 ]] || fail "GDAL must be pinned to 3.13.1"
[[ ${PROJ_VERSION:-} == 9.8.1 ]] || fail "PROJ must be pinned to 9.8.1"
[[ ${ZSTD_VERSION:-} == 1.5.7 ]] || fail "ZSTD must be pinned to 1.5.7"

for variable in GDAL_SHA256 PROJ_SHA256 ZSTD_SHA256; do
    value=${!variable:-}
    [[ $value =~ ^[0-9a-f]{64}$ ]] || fail "$variable must contain a SHA-256 value"
done

[[ ${GDAL_RELOCATABLE_PATCH_SHA256:-} =~ ^[0-9a-f]{64}$ ]] ||
    fail "GDAL_RELOCATABLE_PATCH_SHA256 must contain SHA-256"
[[ ${GDAL_PREFETCH_PATCH_SHA256:-} =~ ^[0-9a-f]{64}$ ]] ||
    fail "GDAL_PREFETCH_PATCH_SHA256 must contain SHA-256"

assert_contains "$prefetch_patch" 'OSGSOL_VSICURL_PREFETCH_HEAD_RANGE' \
    "prefetch patch must consume the path-specific opt-in"
assert_contains "$prefetch_patch" 'CURLOPT_PIPEWAIT' \
    "prefetch Range must wait for HTTP/2 multiplexing"
assert_contains "$prefetch_patch" 'ParallelHeadRangeResult' \
    "prefetch patch must expose explicit coordinator state"
assert_contains "$prefetch_patch" 'bHeadDone' \
    "prefetch coordinator must require HEAD completion"
assert_contains "$prefetch_patch" 'bRangeDone' \
    "prefetch coordinator must require Range completion"
assert_contains "$prefetch_patch" 'CURLINFO_HTTP_VERSION' \
    "prefetch coordinator must prove HTTP/2 on both handles"
assert_contains "$prefetch_patch" 'CURLINFO_CONN_ID' \
    "prefetch coordinator must prove one shared libcurl connection"
assert_contains "$prefetch_patch" 'CURLINFO_REDIRECT_COUNT' \
    "prefetch coordinator must discard redirected prefetches"
assert_contains "$prefetch_patch" 'writer-overflow' \
    "prefetch patch must distinguish a capped-writer abort"
assert_contains "$prefetch_patch" 'nParsedTotal <= nEnd' \
    "Content-Range total must extend beyond the returned interval"
assert_contains "$prefetch_patch" 'cleanup-success=%s' \
    "prefetch patch must expose successful role-aware cleanup proof"
assert_contains "$prefetch_patch" 'cleanup-blocked=%s reason=attached' \
    "prefetch patch must block cleanup while a handle remains attached"
assert_contains "$prefetch_patch" 'file-property-published' \
    "prefetch patch must expose file-property publication proof"
assert_contains "$prefetch_patch" 'eHeadRemove == CURLM_OK' \
    "prefetch patch must verify first-HEAD detach success"
assert_contains "$prefetch_patch" 'eRangeRemove == CURLM_OK' \
    "prefetch patch must verify Range detach success"
assert_contains "$prefetch_patch" 'cleanup-call=%s' \
    "prefetch patch must count every role-aware cleanup call"
assert_contains "$prefetch_patch" 'attempt=2 code=%d' \
    "prefetch patch must retry a failed detach before abandonment"
assert_contains "$prefetch_patch" 'AbandonCurlMultiHandle' \
    "prefetch patch must quarantine a persistently attached cached multi"
assert_contains "$prefetch_patch" 'bParallelHeadRangeDisabled = true' \
    "prefetch patch must bound abandonment to one parallel attempt per thread and filesystem"
assert_contains "$prefetch_patch" 'file-property-publication-blocked' \
    "prefetch patch must block metadata publication after multi abandonment"
assert_not_contains "$prefetch_patch" 'PREFETCH_TEST_' \
    "prefetch patch must not ship runtime fault-injection controls"
assert_contains "$prefetch_patch" 'AddRegion\(m_pszURL, 0, 131072' \
    "validated prefetch bytes must use the existing region cache"
assert_contains "$prefetch_patch" \
    'ParallelHeadRange: logical-get-complete bytes=%zu' \
    "prefetch patch must expose one exact logical GET completion format"
assert_contains "$prefetch_patch" \
    'static bool IsParallelHeadRangeTransientStatus\(long nStatus\)' \
    "prefetch patch must centralize coordinator transient status classification"
classifier=$(sed -n \
    '/^+static bool IsParallelHeadRangeTransientStatus(long nStatus)/,/^+}/p' \
    "$prefetch_patch")
for status in 429 500 502 503 504; do
    [[ $(grep -Ec "^\\+        case ${status}:$" <<<"$classifier") -eq 1 ]] ||
        fail "prefetch patch must classify transient status ${status} exactly once"
done
[[ $(grep -Ec '^\+        case [0-9]+:$' <<<"$classifier") -eq 5 ]] ||
    fail "prefetch patch transient classifier must contain exactly five statuses"
assert_contains "$prefetch_patch" \
    'ParallelHeadRange: transient-retry range=bytes=0-131071 ' \
    "prefetch patch must emit the exact coordinator retry prefix"
assert_contains "$prefetch_patch" \
    'status=%ld bytes=%zu attempt=%d delay-ms=%lld ' \
    "prefetch patch must emit exact coordinator retry numeric fields"
assert_contains "$prefetch_patch" \
    'range-connection=' \
    "prefetch patch must emit coordinator retry connection evidence"
assert_contains "$prefetch_patch" \
    'ParallelHeadRange: transient-fallback range=bytes=0-131071 status=%ld bytes=%zu' \
    "prefetch patch must retain the exact terminal fallback contract"
assert_contains "$prefetch_patch" \
    'ParallelHeadRange: transport head-connection=' \
    "prefetch patch must expose the role-labelled transport prefix"
assert_contains "$prefetch_patch" \
    'range-connection=" CPL_FRMT_GIB' \
    "prefetch transport event must label the Range connection ID"
assert_contains "$prefetch_patch" 'head-http=%d range-http=%d' \
    "prefetch transport event must expose canonical HTTP majors"
assert_contains "$prefetch_patch" 'CURL_HTTP_VERSION_1_0' \
    "prefetch transport event must canonicalize HTTP/1.0"
assert_contains "$prefetch_patch" 'CURL_HTTP_VERSION_1_1' \
    "prefetch transport event must canonicalize HTTP/1.1"
log_get_line=$(grep -n 'NetworkStatisticsLogger::LogGET(nCoordinatorDownloadedBytes)' \
    "$prefetch_patch" | cut -d: -f1)
logical_event_line=$(grep -n 'ParallelHeadRange: logical-get-complete bytes=%zu' \
    "$prefetch_patch" | cut -d: -f1)
[[ -n $log_get_line && -n $logical_event_line &&
      $logical_event_line -eq $((log_get_line + 2)) ]] ||
    fail "logical GET event must be adjacent to its NetworkStatisticsLogger call"

assert_contains "$prefetch_trace" \
    'https://data\.source\.coop/tge-labs/aef/v1/annual/2025/10N/' \
    "prefetch replay must retain the public object URI"
assert_contains "$prefetch_trace" \
    'proxy 127\.0\.0\.1|host 127\.0\.0\.1 left intact' \
    "prefetch replay must retain only the loopback proxy class"
for replay_fixture in "$prefetch_trace" "$prefetch_stats" \
                      "$prefetch_transient_trace" "$prefetch_transient_stats" \
                      "$prefetch_retry_trace" "$prefetch_retry_stats"; do
    assert_not_contains "$replay_fixture" \
        '(^|[^[:alpha:]])(Authorization|Proxy-Authorization|Bearer|token|key|password)([^[:alpha:]]|$)' \
        "prefetch replay fixtures must not contain credential fields"
    assert_not_contains "$replay_fixture" \
        'https?://[^[:space:]]*[?]|https?:\\/\\/[^[:space:]]*[?]' \
        "prefetch replay fixtures must not contain signed query syntax"
    assert_not_contains "$replay_fixture" \
        'https?://[^/@[:space:]]+@|https?:\\/\\/[^/@[:space:]]+@' \
        "prefetch replay fixtures must not contain URI userinfo syntax"
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
assert_contains "$builder" 'ffile-prefix-map' \
    "dependency build must normalize compiled source paths"
assert_contains "$builder" 'verify_no_workspace_strings' \
    "verify must scan linked runtime artifacts for workspace strings"
assert_contains "$builder" 'verify_gdal_pkgconfig_metadata' \
    "verify must validate installed GDAL pkg-config metadata"
assert_contains "$builder" 'lib/pkgconfig/gdal\.pc' \
    "verify must inspect the installed GDAL pkg-config file"
assert_contains "$builder" 'CONFIG_INST_PREFIX' \
    "verify must require GDAL pkg-config metadata to retain the private prefix"
assert_contains "$builder" 'gdal-3.13.1-relocatable-static.patch' \
    "builder must apply the pinned relocatable GDAL patch"
assert_contains "$builder" 'gdal-3.13.1-parallel-head-range.patch' \
    "builder must apply the pinned parallel HEAD/Range patch"
assert_contains "$builder" 'GDAL_PREFETCH_PATCH_SHA256' \
    "builder must verify the independent prefetch patch checksum"
shapelib_apply_line=$(grep -n 'patch --batch --forward -p1.*gdal-3.13.1-disable-shapelib.patch' \
    "$builder" | cut -d: -f1)
relocatable_apply_line=$(grep -n \
    'patch --batch --forward -p1.*gdal-3.13.1-relocatable-static.patch' \
    "$builder" | cut -d: -f1)
prefetch_apply_line=$(grep -n 'patch --batch --forward -p1.*gdal-3.13.1-parallel-head-range.patch' \
    "$builder" | cut -d: -f1)
[[ -n $shapelib_apply_line && -n $relocatable_apply_line && -n $prefetch_apply_line &&
      $shapelib_apply_line -lt $relocatable_apply_line &&
      $relocatable_apply_line -lt $prefetch_apply_line ]] ||
    fail "prefetch patch must apply after Shapelib-disable and relocatable patches"
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
assert_contains "$builder" 'runtime_probe.*active_raster_drivers' \
    "manifest driver claims must come from runtime-probe output"
assert_contains "$builder" 'runtime_probe.*active_remote_vfs' \
    "manifest remote VFS claims must come from runtime-probe output"
assert_contains "$builder" '"active_ogr_drivers": runtime_probe\["active_ogr_drivers"\]' \
    "manifest OGR activity must come from runtime-probe output"
assert_contains "$builder" '"inactive_compiled_helpers"' \
    "manifest must disclose compiled but inactive archive helper surfaces"
for symbol in GDALRegister_COG VSIInstallS3FileHandler VSIInstallGSFileHandler \
        VSIInstallAzureFileHandler VSIInstallOSSFileHandler VSIInstallSwiftFileHandler; do
    assert_contains "$builder" "$symbol" \
        "inactive compiled helper disclosure must be verified against $symbol"
done
assert_not_contains "$builder" \
    '"compiled_raster_drivers"|"compiled_ogr_drivers"|"virtual_file_systems"' \
    "runtime-active results must not be mislabeled as compiled absence"
assert_contains "$runtime_probe" 'active_raster_drivers' \
    "runtime probe must identify its raster-driver result as active"
assert_contains "$runtime_probe" 'active_ogr_drivers' \
    "runtime probe must explicitly report active OGR drivers"
assert_not_contains "$runtime_probe" '\\"active_drivers\\"' \
    "runtime probe must not use the ambiguous active_drivers field"
assert_contains "$builder" 'gdal-embed-capability' \
    "GDAL embedding must be tied to an independent compiler probe"
assert_contains "$builder" 'cache_expect.*ENABLE_GNM.*OFF' \
    "verify must require GNM disabled in the resolved cache"
assert_contains "$builder" 'cache_expect.*GDAL_USE_SHAPELIB_INTERNAL.*OFF' \
    "verify must require internal Shapelib disabled"
assert_contains "$builder" 'EMBED_RESOURCE_FILES.*gdal_embed_supported' \
    "GDAL cache embedding must match the independent probe"
assert_contains "$builder" '_TEST_SHARP_EMBED.*gdal_embed_supported' \
    "GDAL CMake embed result must consume the independent probe under path mapping"
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
