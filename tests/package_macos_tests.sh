#!/bin/bash
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
    echo "SKIP: macOS packaging test"
    exit 0
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${OSGVERSE_SDK:-$ROOT/build/sdk_core}"
DEFAULT_RUNTIME_SDK="$SDK"
if command -v brew >/dev/null 2>&1; then
    BREW_RUNTIME_SDK="$(brew --prefix open-scene-graph 2>/dev/null || true)"
    if [ -n "$BREW_RUNTIME_SDK" ]; then
        DEFAULT_RUNTIME_SDK="$BREW_RUNTIME_SDK"
    fi
fi
RUNTIME_SDK="${OSG_RUNTIME_SDK:-${OSG_ROOT:-$DEFAULT_RUNTIME_SDK}}"
TMP_ROOT="$(mktemp -d -t osgsol-package-contract.XXXXXX)"
APP="$TMP_ROOT/osgSol Earth.app"
LOG="$TMP_ROOT/package.log"
HOME_DIR="$TMP_ROOT/home"
CAPTURE="/tmp/earth_capture_0.png"
VERSION="${OSGSOL_PACKAGE_VERSION:-0.3.0}"
CHANNEL="${OSGSOL_BUILD_CHANNEL:-manual-test}"
SOURCE_COMMIT="${OSGSOL_SOURCE_COMMIT:-$(git -C "$ROOT" rev-parse HEAD)}"
ALPHAEARTH_INDEX="${OSGSOL_ALPHAEARTH_INDEX:-$ROOT/build/science-index-full/alphaearth.sqlite}"
ALPHAEARTH_INDEX_SHA256="$(shasum -a 256 "$ALPHAEARTH_INDEX" | awk '{print $1}')"
mkdir -p "$HOME_DIR"
cleanup()
{
    local status=$?
    if [ "$status" -ne 0 ] && [ -f "$LOG" ]; then
        echo "--- package test log ---" >&2
        tail -200 "$LOG" >&2 || true
    fi
    rm -f "$CAPTURE"
    rm -rf "$TMP_ROOT"
    exit "$status"
}
trap cleanup EXIT

package_candidate()
{
    env -u EARTH_AI_KEY \
        OSGVERSE_SDK="$SDK" \
        OSG_RUNTIME_SDK="$RUNTIME_SDK" \
        OSGSOL_PACKAGE_OUTPUT="$APP" \
        OSGSOL_PACKAGE_VERSION="$VERSION" \
        OSGSOL_BUILD_CHANNEL="$CHANNEL" \
        OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" \
        OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"
}

# A rejected invocation must leave an existing output untouched.
mkdir -p "$APP"
printf '%s\n' "known-good" > "$APP/keep"
if EARTH_AI_KEY="wave0-secret-must-not-ship" \
   OSGVERSE_SDK="$SDK" OSG_RUNTIME_SDK="$RUNTIME_SDK" \
   OSGSOL_PACKAGE_OUTPUT="$APP" OSGSOL_PACKAGE_VERSION="$VERSION" \
   OSGSOL_BUILD_CHANNEL="$CHANNEL" OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" \
   OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
   OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
   bash "$ROOT/packaging/package_macos.sh" >"$LOG" 2>&1; then
    echo "FAIL: packaging accepted EARTH_AI_KEY" >&2
    exit 1
fi
grep -q "Refusing to package while EARTH_AI_KEY is set" "$LOG"
grep -q "known-good" "$APP/keep"

if env -u EARTH_AI_KEY \
   OSGVERSE_SDK="$TMP_ROOT/missing-install" \
   OSG_RUNTIME_SDK="$RUNTIME_SDK" \
   OSGSOL_PACKAGE_OUTPUT="$APP" OSGSOL_PACKAGE_VERSION="$VERSION" \
   OSGSOL_BUILD_CHANNEL="$CHANNEL" OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" \
   OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
   OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
   bash "$ROOT/packaging/package_macos.sh" >"$LOG" 2>&1; then
    echo "FAIL: packaging accepted a missing install SDK" >&2
    exit 1
fi
grep -q "Install SDK is incomplete" "$LOG"
grep -q "known-good" "$APP/keep"

if env -u EARTH_AI_KEY \
   OSGVERSE_SDK="$SDK" OSG_RUNTIME_SDK="$TMP_ROOT/missing-runtime" \
   OSGSOL_PACKAGE_OUTPUT="$APP" OSGSOL_PACKAGE_VERSION="$VERSION" \
   OSGSOL_BUILD_CHANNEL="$CHANNEL" OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" \
   OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
   OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
   bash "$ROOT/packaging/package_macos.sh" >"$LOG" 2>&1; then
    echo "FAIL: packaging accepted a missing OSG runtime SDK" >&2
    exit 1
fi
grep -q "OSG runtime SDK is incomplete" "$LOG"
grep -q "known-good" "$APP/keep"

if env -u EARTH_AI_KEY \
   OSGVERSE_SDK="$SDK" OSG_RUNTIME_SDK="$RUNTIME_SDK" \
   OSGSOL_PACKAGE_OUTPUT="$APP" OSGSOL_PACKAGE_VERSION="$VERSION" \
   OSGSOL_BUILD_CHANNEL="$CHANNEL" OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" \
   OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
   OSGSOL_ALPHAEARTH_INDEX="$TMP_ROOT/missing-alphaearth.sqlite" \
   bash "$ROOT/packaging/package_macos.sh" >"$LOG" 2>&1; then
    echo "FAIL: packaging accepted a missing AlphaEarth index" >&2
    exit 1
fi
grep -q "AlphaEarth index is required" "$LOG"
grep -q "known-good" "$APP/keep"

rm -rf "$APP"
package_candidate

test "$(basename "$APP")" = "osgSol Earth.app"
test -x "$APP/Contents/MacOS/osgSol_Earth"
test ! -e "$APP/Contents/MacOS/osgVerse_EarthExplorer"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleName' "$APP/Contents/Info.plist")" = \
    "osgSol Earth"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleDisplayName' "$APP/Contents/Info.plist")" = \
    "osgSol Earth"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")" = \
    "com.anloren.osgsol.earth"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Contents/Info.plist")" = \
    "osgSol_Earth"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")" = \
    "$VERSION"
test "$(/usr/libexec/PlistBuddy -c 'Print :ScienceEarthBuildChannel' "$APP/Contents/Info.plist")" = \
    "$CHANNEL"
test "$(/usr/libexec/PlistBuddy -c 'Print :ScienceEarthSourceCommit' "$APP/Contents/Info.plist")" = \
    "$SOURCE_COMMIT"
test "$(/usr/libexec/PlistBuddy -c 'Print :ScienceEarthIndexSha256' "$APP/Contents/Info.plist")" = \
    "$ALPHAEARTH_INDEX_SHA256"
test "$(shasum -a 256 "$APP/Contents/misc/science/alphaearth/alphaearth.sqlite" | awk '{print $1}')" = \
    "$ALPHAEARTH_INDEX_SHA256"
if /usr/libexec/PlistBuddy -c 'Print :LSEnvironment:EARTH_AI_KEY' \
   "$APP/Contents/Info.plist" >/dev/null 2>&1; then
    echo "FAIL: plist contains EARTH_AI_KEY" >&2
    exit 1
fi

while IFS= read -r -d '' binary; do
    if file -b "$binary" | grep -q 'Mach-O'; then
        codesign --verify --strict "$binary"
        if otool -L "$binary" | tail -n +2 | awk '{print $1}' |
           grep -F "$RUNTIME_SDK/" >/dev/null; then
            echo "FAIL: packaged Mach-O retains OSG runtime SDK path: $binary" >&2
            exit 1
        fi
        if otool -L "$binary" | tail -n +2 | awk '{print $1}' |
           grep -F "$SDK/" >/dev/null; then
            echo "FAIL: packaged Mach-O retains install SDK path: $binary" >&2
            exit 1
        fi
        if otool -l "$binary" |
           awk '$1 == "cmd" && $2 == "LC_RPATH" { wanted = 1; next }
                wanted && $1 == "path" { print $2; wanted = 0 }' |
           grep -E '^/' >/dev/null; then
            echo "FAIL: packaged Mach-O retains an absolute LC_RPATH: $binary" >&2
            exit 1
        fi
    fi
done < <(find "$APP/Contents" -type f -print0)

env -u EARTH_AI_KEY -u OSG_LIBRARY_PATH HOME="$HOME_DIR" EARTH_IME=0 \
    EARTH_OFFSCREEN=1 EARTH_AUTOCAP=100 \
    "$APP/Contents/MacOS/osgSol_Earth" >"$LOG" 2>&1
grep -q "\[Earth\] offscreen context 1920x1080" "$LOG"
grep -q "\[Earth\] offscreen capture saved" "$LOG"
if grep -Eq "glCompileShader .* FAILED|version '.*' is not supported|OpenGL error 'invalid operation'|Main earth scene is missing|Stack trace" "$LOG"; then
    echo "FAIL: packaged render smoke logged a shader/OpenGL failure" >&2
    exit 1
fi
test -s "$CAPTURE"
if find "$APP" -name imgui.ini -print -quit | grep -q .; then
    echo "FAIL: runtime wrote imgui.ini into signed bundle" >&2
    exit 1
fi
codesign --verify --deep --strict "$APP"
echo "[OK] formal macOS staging identity, credential, smoke, and signature checks"
