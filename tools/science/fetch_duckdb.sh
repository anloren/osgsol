#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "${script_dir}/../.." && pwd -P)"
manifest="${script_dir}/duckdb-manifest.json"
tools_dir="${repo_root}/build/science-tools"
archive_dir="${tools_dir}/downloads"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    printf 'fetch_duckdb.sh: pinned tool supports macOS arm64 only\n' >&2
    exit 1
fi

readarray=()
while IFS= read -r value; do readarray+=("${value}"); done < <(
    python3 - "${manifest}" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as stream:
    item = json.load(stream)["duckdb"]
for key in ("version", "archive", "url", "sha256", "size", "binary"):
    print(item[key])
PY
)
version="${readarray[0]}"
archive_name="${readarray[1]}"
url="${readarray[2]}"
expected_sha256="${readarray[3]}"
expected_size="${readarray[4]}"
binary_name="${readarray[5]}"
archive_path="${archive_dir}/${archive_name}"
binary_path="${tools_dir}/${binary_name}"

mkdir -p "${archive_dir}"
if [[ ! -f "${archive_path}" ]]; then
    temporary_archive="${archive_path}.part.$$"
    trap 'rm -f "${temporary_archive:-}"' EXIT
    curl --fail --location --retry 3 --output "${temporary_archive}" "${url}"
    mv "${temporary_archive}" "${archive_path}"
fi

actual_size="$(stat -f '%z' "${archive_path}")"
actual_sha256="$(shasum -a 256 "${archive_path}" | awk '{print $1}')"
[[ "${actual_size}" == "${expected_size}" ]] || {
    printf 'fetch_duckdb.sh: archive size mismatch: expected %s, got %s\n' \
        "${expected_size}" "${actual_size}" >&2
    exit 1
}
[[ "${actual_sha256}" == "${expected_sha256}" ]] || {
    printf 'fetch_duckdb.sh: archive checksum mismatch\n' >&2
    exit 1
}

extract_dir="${tools_dir}/.duckdb-extract.$$"
trap 'rm -rf "${extract_dir:-}" "${temporary_archive:-}"' EXIT
mkdir -p "${extract_dir}"
unzip -q "${archive_path}" -d "${extract_dir}"
test -f "${extract_dir}/${binary_name}"
chmod 0755 "${extract_dir}/${binary_name}"
mv "${extract_dir}/${binary_name}" "${binary_path}"
printf '%s\n' "${version}" > "${tools_dir}/duckdb.version"
"${binary_path}" --version >/dev/null
printf '%s\n' "${binary_path}"
