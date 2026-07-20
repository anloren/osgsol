#!/usr/bin/env bash

set -u

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
script_path="${script_directory}/$(basename "${BASH_SOURCE[0]}")"
repo_root="$(cd "${script_directory}/.." && pwd -P)"
baseline_document="${repo_root}/docs/scienceearth/g0-g1-baseline.md"
reference_manifest="${repo_root}/packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json"
ratchet_manifest="${repo_root}/packaging/scienceearth/baselines/current-macos-arm64-ratchet.json"
manifest_module="${repo_root}/packaging/scienceearth/g0_manifest.py"
release_descriptor="${repo_root}/packaging/osgsol_release.env"
verification_document="${repo_root}/docs/scienceearth/v0.6.1-verification.md"
failures=0

fail()
{
    printf '[FAIL] %s\n' "$1" >&2
    failures=1
}

validate_release_pair()
{
    local normal_ref="$1"
    local scienceearth_ref="$2"
    local normal_commit
    local scienceearth_commit

    if ! normal_commit="$(git -C "${repo_root}" rev-parse --verify "${normal_ref}^{}" 2>/dev/null)"; then
        printf '[FAIL] release ref does not resolve: %s\n' "${normal_ref}" >&2
        return 1
    fi
    if ! scienceearth_commit="$(git -C "${repo_root}" rev-parse --verify \
            "${scienceearth_ref}^{}" 2>/dev/null)"; then
        printf '[FAIL] ScienceEarth release ref does not resolve: %s\n' \
            "${scienceearth_ref}" >&2
        return 1
    fi

    if [[ "${normal_commit}" != "${scienceearth_commit}" ]]; then
        printf '[FAIL] release refs dereference to different commits: %s != %s\n' \
            "${normal_ref}" "${scienceearth_ref}" >&2
        return 1
    fi

    printf '[OK] release refs share commit %s\n' "${normal_commit}"
}

descriptor_value()
{
    local key="$1"
    awk -F= -v wanted="${key}" '$1 == wanted { print substr($0, length($1) + 2) }' \
        "${release_descriptor}"
}

validate_release_names()
{
    local normal_ref="$1"
    local scienceearth_ref="$2"
    local version
    version="$(descriptor_value OSGSOL_PRODUCT_VERSION)"
    [[ "${normal_ref}" == "v${version}" &&
       "${scienceearth_ref}" == "ScienceEarth-v${version}" ]]
}

validate_manifests()
{
    local reference_path="$1"
    local ratchet_path="$2"
    local immutable_commit

    immutable_commit="$(git -C "${repo_root}" rev-parse 'ScienceEarth^{}')" || return 1
    python3 - "${manifest_module}" "${reference_path}" \
            "${ratchet_path}" "${immutable_commit}" <<'PY'
import importlib.util
import json
import sys

module_path, reference_path, ratchet_path, immutable_commit = sys.argv[1:]
spec = importlib.util.spec_from_file_location("scienceearth_g0_manifest", module_path)
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)
with open(reference_path, encoding="utf-8") as stream:
    reference = json.load(stream)
with open(ratchet_path, encoding="utf-8") as stream:
    ratchet = json.load(stream)
manifest.validate_chain(reference, ratchet)
manifest.validate_approved_chain(
    reference, ratchet, reference_path, ratchet_path)
if reference["source_commit"] != immutable_commit:
    raise ValueError(
        "reference source commit does not match the immutable ScienceEarth tag")
PY
}

if [[ $# -gt 0 ]]; then
    case "$1" in
        --validate-release-pair)
            [[ $# -eq 3 ]] || exit 64
            validate_release_pair "$2" "$3"
            exit $?
            ;;
        --validate-manifests)
            [[ $# -eq 3 ]] || exit 64
            validate_manifests "$2" "$3"
            exit $?
            ;;
        --validate-release-names)
            [[ $# -eq 3 ]] || exit 64
            validate_release_names "$2" "$3"
            exit $?
            ;;
        *)
            printf 'usage: %s --validate-release-pair REF REF | \
--validate-manifests REFERENCE RATCHET | \
--validate-release-names TAG SCIENCE_TAG\n' "${BASH_SOURCE[0]}" >&2
            exit 64
            ;;
    esac
fi

cd "${repo_root}"

[[ -f "${release_descriptor}" ]] || \
    fail "canonical release descriptor is missing"
[[ -f "${verification_document}" ]] || \
    fail "v0.6.1 verification document is missing"
if [[ -f "${release_descriptor}" ]]; then
    [[ "$(descriptor_value OSGSOL_PRODUCT_VERSION)" == "0.6.1" ]] ||
        fail "release descriptor product version disagrees"
    [[ "$(descriptor_value OSGSOL_SCIENCE_PHASE)" == "G3.1" ]] ||
        fail "release descriptor science phase disagrees"
    [[ "$(descriptor_value OSGSOL_PRODUCT_NAME)" == "osgSol Earth" ]] ||
        fail "release descriptor product name disagrees"
    [[ "$(descriptor_value OSGSOL_BUNDLE_ID)" == \
        "com.anloren.osgsol.earth" ]] ||
        fail "release descriptor bundle id disagrees"
fi

if ! validate_release_names v0.6.1 ScienceEarth-v0.6.1; then
    fail "formal paired tag names do not match the release descriptor"
fi
if validate_release_names v0.6.0 ScienceEarth-v0.6.1; then
    fail "paired tag validation accepted a version mismatch"
fi

if [[ -f "${verification_document}" ]]; then
    grep -Fq 'Product version: `0.6.1`' "${verification_document}" ||
        fail "verification document version disagrees"
    grep -Fq 'Science phase: `G3.1`' "${verification_document}" ||
        fail "verification document phase disagrees"
fi

grep -Fq 'INCLUDE("${CMAKE_SOURCE_DIR}/cmake/OsgSolRelease.cmake")' \
    CMakeLists.txt || fail "CMake does not load the canonical release descriptor"
grep -Fq 'packaging/osgsol_release.env' packaging/package_macos.sh ||
    fail "packaging does not load the canonical release descriptor"
if grep -Fq 'VERSION="${OSGSOL_PACKAGE_VERSION:-0.3.0}"' \
        packaging/package_macos.sh; then
    fail "packaging retains the stale 0.3.0 default"
fi
if rg -n 'std::abort\(|(^|[^[:alnum:]_])abort\(' tests \
        --glob '*.cpp' --glob '*.h' >/dev/null; then
    fail "test executables must report normal failure instead of generating macOS crash reports"
fi
if rg -n 'LABELS "[^"]*offline[^"]*network-local|LABELS "[^"]*network-local[^"]*offline' \
        tests/CMakeLists.txt >/dev/null; then
    fail "loopback-listener tests must not be included in the offline no-port suite"
fi

test "$(git rev-list -n 1 v0.2.0)" = "$(git rev-list -n 1 ScienceEarth)" || \
    fail "v0.2.0 and ScienceEarth do not resolve to the same commit"
test "$(git rev-parse 'ScienceEarth^{}')" = \
     "0e91c7c4b121d80b929d595ea711d3dd0833ee67" || \
    fail "ScienceEarth moved from the immutable v0.2.0 boundary"

[[ -f "${baseline_document}" ]] || \
    fail "baseline evidence document is missing: docs/scienceearth/g0-g1-baseline.md"
[[ -f "${reference_manifest}" ]] || \
    fail "immutable reference manifest is missing: \
packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json"
[[ -f "${ratchet_manifest}" ]] || \
    fail "current ratchet manifest is missing: \
packaging/scienceearth/baselines/current-macos-arm64-ratchet.json"

if [[ -f "${reference_manifest}" && -f "${ratchet_manifest}" ]]; then
    if ! validate_manifests "${reference_manifest}" "${ratchet_manifest}"; then
        fail "committed G0 reference/ratchet manifest contract is invalid"
    fi
fi

if ! "${script_path}" --validate-release-pair v0.2.0 ScienceEarth; then
    fail "release-pair validation entry point is missing or rejects the valid boundary pair"
fi

if "${script_path}" --validate-release-pair v0.2.0 \
        c2161f3d6b43318e37504f1cc22dfefc96adcd36 >/dev/null 2>&1; then
    fail "release-pair validation accepted refs that dereference to different commits"
fi

if [[ ${failures} -ne 0 ]]; then
    exit 1
fi

printf '[OK] ScienceEarth release boundary contract\n'
