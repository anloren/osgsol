#!/usr/bin/env bash

set -u

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
baseline_document="${repo_root}/docs/scienceearth/g0-g1-baseline.md"
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

if [[ $# -gt 0 ]]; then
    if [[ $# -ne 3 || "$1" != "--validate-release-pair" ]]; then
        printf 'usage: %s --validate-release-pair vX.Y.Z ScienceEarth-vX.Y.Z\n' \
            "${BASH_SOURCE[0]}" >&2
        exit 64
    fi
    validate_release_pair "$2" "$3"
    exit $?
fi

cd "${repo_root}"

test "$(git rev-list -n 1 v0.2.0)" = "$(git rev-list -n 1 ScienceEarth)" || \
    fail "v0.2.0 and ScienceEarth do not resolve to the same commit"
test "$(git rev-parse 'ScienceEarth^{}')" = \
     "0e91c7c4b121d80b929d595ea711d3dd0833ee67" || \
    fail "ScienceEarth moved from the immutable v0.2.0 boundary"

[[ -f "${baseline_document}" ]] || \
    fail "baseline evidence document is missing: docs/scienceearth/g0-g1-baseline.md"

if ! "${BASH_SOURCE[0]}" --validate-release-pair v0.2.0 ScienceEarth; then
    fail "release-pair validation entry point is missing or rejects the valid boundary pair"
fi

if "${BASH_SOURCE[0]}" --validate-release-pair v0.2.0 \
        c2161f3d6b43318e37504f1cc22dfefc96adcd36 >/dev/null 2>&1; then
    fail "release-pair validation accepted refs that dereference to different commits"
fi

if [[ ${failures} -ne 0 ]]; then
    exit 1
fi

printf '[OK] ScienceEarth release boundary contract\n'
