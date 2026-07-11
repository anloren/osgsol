#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "${script_dir}/../.." && pwd -P)"
builder="${script_dir}/build_aef_index.py"
schema="${script_dir}/aef_index_schema.sql"
manifest="${script_dir}/duckdb-manifest.json"
tools_dir="${repo_root}/build/science-tools"
fixture=""
output=""
print_duckdb_sql=""
fetch_source_only=0
temporary_download=""
manifest_temporary=""
trap 'rm -f "${temporary_download:-}" "${manifest_temporary:-}"' EXIT

production_select_sql()
{
    local source_path="$1"
    [[ "${source_path}" != *"'"* ]] || {
        printf 'extract_aef_index.sh: source path contains an unsupported quote\n' >&2
        return 1
    }
    cat <<SQL
SELECT fid, path, year,
       wgs84_west, wgs84_south, wgs84_east, wgs84_north, location
  FROM read_parquet('${source_path}')
 ORDER BY year, fid
SQL
}

usage()
{
    printf 'usage: %s [--fixture input.csv] --output output.sqlite\n' \
        "${BASH_SOURCE[0]}" >&2
    printf '       %s --print-duckdb-sql parquet-path\n' "${BASH_SOURCE[0]}" >&2
    printf '       %s --fetch-source-only [--manifest file] [--tools-dir path]\n' \
        "${BASH_SOURCE[0]}" >&2
    exit 64
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fixture)
            [[ $# -ge 2 ]] || usage
            fixture="$2"
            shift 2
            ;;
        --output)
            [[ $# -ge 2 ]] || usage
            output="$2"
            shift 2
            ;;
        --print-duckdb-sql)
            [[ $# -ge 2 ]] || usage
            print_duckdb_sql="$2"
            shift 2
            ;;
        --fetch-source-only)
            fetch_source_only=1
            shift
            ;;
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

if [[ -n "${print_duckdb_sql}" ]]; then
    [[ -z "${fixture}" && -z "${output}" && ${fetch_source_only} -eq 0 ]] || usage
    production_select_sql "${print_duckdb_sql}"
    exit $?
fi
if [[ ${fetch_source_only} -eq 0 ]]; then
    [[ -n "${output}" ]] || usage
else
    [[ -z "${fixture}" && -z "${output}" ]] || usage
fi

cache_matches()
{
    local path="$1"
    local expected_bytes="$2"
    local expected_hash="$3"
    [[ -f "${path}" ]] || return 1
    [[ "$(stat -f '%z' "${path}")" == "${expected_bytes}" ]] || return 1
    [[ "$(shasum -a 256 "${path}" | awk '{print $1}')" == "${expected_hash}" ]]
}

fetch_verified()
{
    local url="$1"
    local destination="$2"
    local expected_bytes="$3"
    local expected_hash="$4"
    if cache_matches "${destination}" "${expected_bytes}" "${expected_hash}"; then
        return 0
    fi
    rm -f "${destination}"
    temporary_download="${destination}.part.$$"
    rm -f "${temporary_download}"
    curl --fail --location --retry 3 --output "${temporary_download}" "${url}"
    if ! cache_matches "${temporary_download}" "${expected_bytes}" "${expected_hash}"; then
        printf 'extract_aef_index.sh: downloaded source failed size/checksum validation\n' >&2
        rm -f "${temporary_download}"
        return 1
    fi
    mv "${temporary_download}" "${destination}"
    temporary_download=""
}

if [[ -n "${fixture}" ]]; then
    output_dir="$(dirname "${output}")"
    mkdir -p "${output_dir}"
    manifest_output="${output}.manifest.json"
    manifest_temporary="${manifest_output}.tmp.$$"
    fixture="$(cd "$(dirname "${fixture}")" && pwd -P)/$(basename "${fixture}")"
    source_sha256="$(shasum -a 256 "${fixture}" | awk '{print $1}')"
    row_count="$(python3 - "${fixture}" <<'PY'
import csv
import sys
with open(sys.argv[1], newline="", encoding="utf-8") as stream:
    print(sum(1 for _ in csv.DictReader(stream)))
PY
)"
    python3 "${builder}" \
        --input "${fixture}" \
        --output "${output}" \
        --schema "${schema}" \
        --schema-version 2 \
        --source-index-url "https://fixture.invalid/aef_index_fixture.csv" \
        --source-index-sha256 "${source_sha256}" \
        --expected-row-count "${row_count}" > "${manifest_temporary}"
    mv "${manifest_temporary}" "${manifest_output}"
    manifest_temporary=""
    printf '%s\n' "${output}"
    exit 0
fi

mkdir -p "${tools_dir}"
tools_dir="$(cd "${tools_dir}" && pwd -P)"
allowed_root="${repo_root}/build/science-tools"
if [[ "${tools_dir}" != "${allowed_root}" && "${tools_dir}" != "${allowed_root}/"* ]]; then
    printf 'extract_aef_index.sh: tools directory must remain under build/science-tools\n' >&2
    exit 1
fi
values=()
while IFS= read -r value; do values+=("${value}"); done < <(
    python3 - "${manifest}" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as stream:
    item = json.load(stream)["source_index"]
for key in ("url", "sha256", "size"):
    print(item[key])
PY
)
source_url="${values[0]}"
source_sha256="${values[1]}"
source_size="${values[2]}"
source_path="${tools_dir}/aef_index.parquet"
fetch_verified "${source_url}" "${source_path}" "${source_size}" "${source_sha256}"

if [[ ${fetch_source_only} -eq 1 ]]; then
    printf '%s\n' "${source_path}"
    exit 0
fi

output_dir="$(dirname "${output}")"
mkdir -p "${output_dir}"
manifest_output="${output}.manifest.json"
manifest_temporary="${manifest_output}.tmp.$$"
duckdb="$(bash "${script_dir}/fetch_duckdb.sh" \
    --manifest "${manifest}" --tools-dir "${tools_dir}")"
select_sql="$(production_select_sql "${source_path}")"
row_count="$("${duckdb}" -noheader -list -c \
    "SELECT COUNT(*) FROM read_parquet('${source_path}')")"
"${duckdb}" -csv -c "${select_sql}" | \
    python3 "${builder}" \
        --input - \
        --output "${output}" \
        --schema "${schema}" \
        --schema-version 2 \
        --source-index-url "${source_url}" \
        --source-index-sha256 "${source_sha256}" \
        --expected-row-count "${row_count}" > "${manifest_temporary}"
mv "${manifest_temporary}" "${manifest_output}"
manifest_temporary=""
printf '%s\n' "${output}"
