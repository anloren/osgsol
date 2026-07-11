#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "${script_dir}/../.." && pwd -P)"
builder="${script_dir}/build_aef_index.py"
schema="${script_dir}/aef_index_schema.sql"
manifest="${script_dir}/duckdb-manifest.json"
fixture=""
output=""
print_duckdb_sql=""

production_select_sql()
{
    local source_path="$1"
    [[ "${source_path}" != *"'"* ]] || {
        printf 'extract_aef_index.sh: source path contains an unsupported quote\n' >&2
        return 1
    }
    cat <<SQL
SELECT CAST(fid AS VARCHAR) AS dataset_id,
       year,
       replace(path, 's3://us-west-2.opendata.source.coop/',
                     'https://data.source.coop/') AS cog_url,
       regexp_replace(
           replace(path, 's3://us-west-2.opendata.source.coop/',
                         'https://data.source.coop/'),
           '\\.tiff\$', '.vrt') AS vrt_url,
       wgs84_west AS min_lon,
       wgs84_south AS min_lat,
       wgs84_east AS max_lon,
       wgs84_north AS max_lat,
       '1.1' AS source_version,
       sha256(concat_ws('|', CAST(fid AS VARCHAR), CAST(year AS VARCHAR), path,
                        CAST(wgs84_west AS VARCHAR), CAST(wgs84_south AS VARCHAR),
                        CAST(wgs84_east AS VARCHAR), CAST(wgs84_north AS VARCHAR)))
           AS source_checksum
  FROM read_parquet('${source_path}')
 ORDER BY year, dataset_id
SQL
}

usage()
{
    printf 'usage: %s [--fixture input.csv] --output output.sqlite\n' \
        "${BASH_SOURCE[0]}" >&2
    printf '       %s --print-duckdb-sql parquet-path\n' "${BASH_SOURCE[0]}" >&2
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
        *) usage ;;
    esac
done
if [[ -n "${print_duckdb_sql}" ]]; then
    [[ -z "${fixture}" && -z "${output}" ]] || usage
    production_select_sql "${print_duckdb_sql}"
    exit $?
fi
[[ -n "${output}" ]] || usage

output_dir="$(dirname "${output}")"
mkdir -p "${output_dir}"
manifest_output="${output}.manifest.json"
manifest_temporary="${manifest_output}.tmp.$$"
trap 'rm -f "${manifest_temporary:-}"' EXIT

if [[ -n "${fixture}" ]]; then
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
        --schema-version 1 \
        --source-index-url "https://fixture.invalid/aef_index_fixture.csv" \
        --source-index-sha256 "${source_sha256}" \
        --expected-row-count "${row_count}" > "${manifest_temporary}"
else
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
    duckdb="$(bash "${script_dir}/fetch_duckdb.sh")"
    source_path="${repo_root}/build/science-tools/aef_index.parquet"
    if [[ ! -f "${source_path}" ]]; then
        temporary_source="${source_path}.part.$$"
        trap 'rm -f "${manifest_temporary:-}" "${temporary_source:-}"' EXIT
        curl --fail --location --retry 3 --output "${temporary_source}" "${source_url}"
        mv "${temporary_source}" "${source_path}"
    fi
    actual_size="$(stat -f '%z' "${source_path}")"
    actual_sha256="$(shasum -a 256 "${source_path}" | awk '{print $1}')"
    [[ "${actual_size}" == "${source_size}" ]] || {
        printf 'extract_aef_index.sh: source index size mismatch\n' >&2
        exit 1
    }
    [[ "${actual_sha256}" == "${source_sha256}" ]] || {
        printf 'extract_aef_index.sh: source index checksum mismatch\n' >&2
        exit 1
    }

    select_sql="$(production_select_sql "${source_path}")"
    row_count="$("${duckdb}" -noheader -list -c \
        "SELECT COUNT(*) FROM read_parquet('${source_path}')")"
    "${duckdb}" -csv -c "${select_sql}" | \
        python3 "${builder}" \
            --input - \
            --output "${output}" \
            --schema "${schema}" \
            --schema-version 1 \
            --source-index-url "${source_url}" \
            --source-index-sha256 "${source_sha256}" \
            --expected-row-count "${row_count}" > "${manifest_temporary}"
fi

mv "${manifest_temporary}" "${manifest_output}"
printf '%s\n' "${output}"
