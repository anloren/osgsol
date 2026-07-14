#!/bin/bash
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
    echo "SKIP: macOS packaging test"
    exit 0
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${OSGVERSE_SDK:-$ROOT/build/sdk_core}"
APP="$ROOT/dist/EarthExplorer.app"
LOG="$(mktemp -t osgsol-package.XXXXXX)"
HOME_DIR="$(mktemp -d -t osgsol-home.XXXXXX)"
trap 'rm -f "$LOG" /tmp/earth_capture_0.png; rm -rf "$HOME_DIR"' EXIT

rm -rf "$APP"
if EARTH_AI_KEY="wave0-secret-must-not-ship" \
   OSGVERSE_SDK="$SDK" bash "$ROOT/packaging/package_macos.sh" >"$LOG" 2>&1; then
    echo "FAIL: packaging accepted EARTH_AI_KEY" >&2
    exit 1
fi
grep -q "Refusing to package while EARTH_AI_KEY is set" "$LOG"
test ! -e "$APP"

if env -u EARTH_AI_KEY OSG_ROOT="$HOME_DIR/missing-osg-runtime" \
   OSGVERSE_SDK="$SDK" bash "$ROOT/packaging/package_macos.sh" >"$LOG" 2>&1; then
    echo "FAIL: packaging accepted a missing OSG runtime SDK" >&2
    exit 1
fi
grep -q "OSG runtime SDK is incomplete" "$LOG"
test ! -e "$APP"

env -u EARTH_AI_KEY OSGVERSE_SDK="$SDK" \
    bash "$ROOT/packaging/package_macos.sh"
test -x "$APP/Contents/MacOS/osgVerse_EarthExplorer"
if /usr/libexec/PlistBuddy -c 'Print :LSEnvironment:EARTH_AI_KEY' \
   "$APP/Contents/Info.plist" >/dev/null 2>&1; then
    echo "FAIL: plist contains EARTH_AI_KEY" >&2
    exit 1
fi

env -u EARTH_AI_KEY HOME="$HOME_DIR" EARTH_IME=0 \
    EARTH_OFFSCREEN=1 EARTH_AUTOCAP=100 \
    "$APP/Contents/MacOS/osgVerse_EarthExplorer" >"$LOG" 2>&1
grep -q "\[Earth\] offscreen context" "$LOG"
grep -q "\[Earth\] offscreen capture saved" "$LOG"
if grep -Eq "glCompileShader .* FAILED|version '.*' is not supported|OpenGL error 'invalid operation'|Main earth scene is missing|Stack trace" "$LOG"; then
    echo "FAIL: packaged render smoke logged a shader/OpenGL failure" >&2
    exit 1
fi
test -s /tmp/earth_capture_0.png
if find "$APP" -name imgui.ini -print -quit | grep -q .; then
    echo "FAIL: runtime wrote imgui.ini into signed bundle" >&2
    exit 1
fi
codesign --verify --deep --strict "$APP"
echo "[OK] macOS package credential, smoke, and signature checks"
