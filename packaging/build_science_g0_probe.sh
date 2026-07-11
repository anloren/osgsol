#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
BASELINE="${SCIENCE_G0_BASELINE_APP:-/Users/USER/Desktop/osgSol Earth.app}"
CURRENT_EXECUTABLE="${SCIENCE_G0_CURRENT_EXECUTABLE:-\
$ROOT/build/sdk_core/bin/osgVerse_EarthExplorer}"
PROBE_PLUGIN="${SCIENCE_G0_PROBE_PLUGIN:-$ROOT/build/science_g0/lib/osgdb_science_g0_probe.so}"
OUTPUT="${SCIENCE_G0_OUTPUT_APP:-$ROOT/build/science_g0/osgSol Science G0 Probe.app}"
CODESIGN_BIN="${CODESIGN_BIN:-/usr/bin/codesign}"
MAIN_RELATIVE="Contents/MacOS/osgSol_Earth"
PLUGIN_RELATIVE="Contents/lib/osgPlugins-3.6.5/osgdb_science.so"

fail()
{
    echo "[science-g0-probe] $*" >&2
    exit 1
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

"$CODESIGN_BIN" --force --deep --sign - "$OUTPUT"
"$CODESIGN_BIN" --verify --deep --strict "$OUTPUT"

echo "[science-g0-probe] built disposable probe: $OUTPUT"
