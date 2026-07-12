#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
BASELINE="${SCIENCE_G0_BASELINE_APP:-/Users/USER/Desktop/osgSol Earth.app}"
CURRENT_EXECUTABLE="${SCIENCE_G0_CURRENT_EXECUTABLE:-\
$ROOT/build/sdk_core/bin/osgVerse_EarthExplorer}"
PROBE_PLUGIN="${SCIENCE_G0_PROBE_PLUGIN:-$ROOT/build/science_g0/lib/osgdb_science_g0_probe.so}"
OUTPUT="${SCIENCE_G0_OUTPUT_APP:-$ROOT/build/science_g0/osgSol Science G0 Probe.app}"
MAIN_RELATIVE="Contents/MacOS/osgSol_Earth"
PLUGIN_RELATIVE="Contents/lib/osgPlugins-3.6.5/osgdb_science.so"

fail()
{
    echo "[science-g0-probe] $*" >&2
    exit 1
}

bundle_fingerprint()
{
    PYTHONDONTWRITEBYTECODE=1 python3 - \
        "$ROOT/packaging/scienceearth/g0_manifest.py" "$1" <<'PY'
import importlib.util
import sys

module_path, app_path = sys.argv[1:]
spec = importlib.util.spec_from_file_location(
    "scienceearth_g0_manifest_for_probe_builder", module_path)
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)
print(manifest.bundle_fingerprint(app_path))
PY
}

[ -d "$BASELINE" ] || fail "baseline app is missing: $BASELINE"
BASELINE="$(python3 -c \
    'import pathlib, sys; print(pathlib.Path(sys.argv[1]).resolve())' "$BASELINE")"
OUTPUT="$(python3 -c \
    'import pathlib, sys; print(pathlib.Path(sys.argv[1]).resolve())' "$OUTPUT")"
[ -f "$BASELINE/$MAIN_RELATIVE" ] || fail "baseline executable is missing"
[ -x "$CURRENT_EXECUTABLE" ] || fail "current executable is missing or not executable"
[ -f "$PROBE_PLUGIN" ] || fail "science probe plugin is missing"
[ "$OUTPUT" != "$BASELINE" ] || fail "output must not equal the protected baseline"
case "$OUTPUT/" in
    "$BASELINE/"*) fail "output must not be inside the protected baseline" ;;
esac
case "$BASELINE/" in
    "$OUTPUT/"*) fail "output must not contain the protected baseline" ;;
esac
case "$OUTPUT" in
    *.app) ;;
    *) fail "output must be an .app path" ;;
esac
BASELINE_FINGERPRINT_BEFORE="$(bundle_fingerprint "$BASELINE")"

rm -rf "$OUTPUT"
mkdir -p "$(dirname "$OUTPUT")"
/usr/bin/ditto --norsrc --noextattr --noacl --noqtn --nopersistRootless \
    "$BASELINE" "$OUTPUT"
chmod -R u+w "$OUTPUT"
rm -rf "$OUTPUT/Contents/_CodeSignature"
cp "$CURRENT_EXECUTABLE" "$OUTPUT/$MAIN_RELATIVE"
chmod 755 "$OUTPUT/$MAIN_RELATIVE"
mkdir -p "$(dirname "$OUTPUT/$PLUGIN_RELATIVE")"
cp "$PROBE_PLUGIN" "$OUTPUT/$PLUGIN_RELATIVE"
chmod 755 "$OUTPUT/$PLUGIN_RELATIVE"

/usr/bin/codesign --force --deep --sign - "$OUTPUT"
/usr/bin/codesign --verify --deep --strict "$OUTPUT"
BASELINE_FINGERPRINT_AFTER="$(bundle_fingerprint "$BASELINE")"
[ "$BASELINE_FINGERPRINT_BEFORE" = "$BASELINE_FINGERPRINT_AFTER" ] || \
    fail "protected baseline fingerprint changed during probe build"

echo "[science-g0-probe] protected baseline fingerprint unchanged: $BASELINE_FINGERPRINT_AFTER"
echo "[science-g0-probe] built disposable probe: $OUTPUT"
