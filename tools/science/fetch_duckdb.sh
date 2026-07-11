#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "${script_dir}/../.." && pwd -P)"
manifest="${script_dir}/duckdb-manifest.json"
tools_dir="${repo_root}/build/science-tools"
temporary_download=""
extract_dir=""
trap 'rm -rf "${temporary_download:-}" "${extract_dir:-}"' EXIT

usage()
{
    printf 'usage: %s [--manifest manifest.json] [--tools-dir build/science-tools/path]\n' \
        "${BASH_SOURCE[0]}" >&2
    exit 64
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --manifest)
            [[ $# -ge 2 ]] || usage
            manifest="$2"
            shift 2
            ;;
        --tools-dir)
            [[ $# -ge 2 ]] || usage
            tools_dir="$2"
            shift 2
            ;;
        *) usage ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    printf 'fetch_duckdb.sh: pinned tool supports macOS arm64 only\n' >&2
    exit 1
fi

allowed_root="${repo_root}/build/science-tools"
validate_tools_dir()
{
    local requested="$1"
    local resolved
    mkdir -p "${allowed_root}"
    resolved="$(python3 - "${requested}" <<'PY'
import os
import sys
print(os.path.realpath(os.path.abspath(sys.argv[1])))
PY
)"
    if [[ "${resolved}" != "${allowed_root}" && "${resolved}" != "${allowed_root}/"* ]]; then
        printf 'fetch_duckdb.sh: tools directory must remain under build/science-tools\n' >&2
        return 1
    fi
    mkdir -p "${resolved}"
    resolved="$(cd "${resolved}" && pwd -P)"
    if [[ "${resolved}" != "${allowed_root}" && "${resolved}" != "${allowed_root}/"* ]]; then
        printf 'fetch_duckdb.sh: resolved tools directory escaped build/science-tools\n' >&2
        return 1
    fi
    printf '%s\n' "${resolved}"
}
tools_dir="$(validate_tools_dir "${tools_dir}")"
archive_dir="${tools_dir}/downloads"

values=()
if ! manifest_values="$(python3 - "${manifest}" <<'PY'
import json
import os
import sys
with open(sys.argv[1], encoding="utf-8") as stream:
    item = json.load(stream)["duckdb"]
for key in ("archive", "binary"):
    value = item[key]
    if (not isinstance(value, str) or not value or value in (".", "..") or
            value != os.path.basename(value) or "/" in value or "\\" in value or
            any(ord(character) < 32 or ord(character) == 127 for character in value)):
        raise SystemExit("fetch_duckdb.sh: unsafe {} name in manifest".format(key))
for key in ("version", "archive", "url", "sha256", "size", "binary"):
    print(item[key])
PY
)"; then
    exit 1
fi
while IFS= read -r value; do values+=("${value}"); done <<< "${manifest_values}"
version="${values[0]}"
archive_name="${values[1]}"
url="${values[2]}"
expected_sha256="${values[3]}"
expected_size="${values[4]}"
binary_name="${values[5]}"
archive_path="${archive_dir}/${archive_name}"
binary_path="${tools_dir}/${binary_name}"

cache_matches()
{
    local path="$1"
    local expected_bytes="$2"
    local expected_hash="$3"
    [[ -f "${path}" ]] || return 1
    [[ "$(stat -f '%z' "${path}")" == "${expected_bytes}" ]] || return 1
    [[ "$(shasum -a 256 "${path}" | awk '{print $1}')" == "${expected_hash}" ]]
}

mkdir -p "${archive_dir}"
if ! cache_matches "${archive_path}" "${expected_size}" "${expected_sha256}"; then
    rm -f "${archive_path}"
    temporary_download="${archive_path}.part.$$"
    rm -f "${temporary_download}"
    curl --fail --location --retry 3 --output "${temporary_download}" "${url}"
    if ! cache_matches "${temporary_download}" "${expected_size}" "${expected_sha256}"; then
        printf 'fetch_duckdb.sh: downloaded archive failed size/checksum validation\n' >&2
        rm -f "${temporary_download}"
        exit 1
    fi
    mv "${temporary_download}" "${archive_path}"
    temporary_download=""
fi

extract_dir="${tools_dir}/.duckdb-extract.$$"
mkdir -p "${extract_dir}"
unzip -q "${archive_path}" "${binary_name}" -d "${extract_dir}"
test -f "${extract_dir}/${binary_name}"
chmod 0755 "${extract_dir}/${binary_name}"
mv "${extract_dir}/${binary_name}" "${binary_path}"
printf '%s\n' "${version}" > "${tools_dir}/duckdb.version"
"${binary_path}" --version >/dev/null
printf '%s\n' "${binary_path}"
