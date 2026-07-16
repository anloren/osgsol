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
PLUGVER="osgPlugins-3.6.5"
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

expect_rejected_profile()
{
    local expected="$1"
    shift
    if "$@" >"$LOG" 2>&1; then
        echo "FAIL: packaging accepted $expected" >&2
        exit 1
    fi
    grep -q "$expected" "$LOG"
    grep -q "known-good" "$APP/keep"
}

collect_osg_uuid_records()
{
    local root="$1"
    local dylib name family
    while IFS= read -r -d '' dylib; do
        name="$(basename "$dylib")"
        family="${name%%.*}"
        dwarfdump --uuid "$dylib" |
            awk -v family="$family" \
                '$1 == "UUID:" {gsub(/[()]/, "", $3); print family "\t" $3 "\t" $2}'
    done < <(find -L "$root" -maxdepth 1 -type f \
        \( -name 'libOpenThreads*.dylib' -o -name 'libosg*.dylib' \) -print0)
}

assert_unique_family_arch()
{
    local records="$1"
    awk -F '\t' '
        {
            key = $1 FS $2
            if (key in seen && seen[key] != $3) {
                print "multiple UUIDs for " $1 " " $2 ": " seen[key] " and " $3 > "/dev/stderr"
                bad = 1
            }
            seen[key] = $3
        }
        END { exit bad }
    ' "$records"
}

audit_unique_macho_uuids()
{
    local app="$1"
    local records="$TMP_ROOT/all-macho-uuids"
    local binary duplicates
    : > "$records"
    while IFS= read -r -d '' binary; do
        if file -b "$binary" | grep -q 'Mach-O'; then
            dwarfdump --uuid "$binary" >> "$records"
        fi
    done < <(find "$app/Contents" -type f -print0)
    test -s "$records"
    duplicates="$(awk '$1 == "UUID:" {print $2}' "$records" | sort | uniq -d)"
    test -z "$duplicates"
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

# Compile-time and runtime OSG profiles are separate inputs. A formal package must reject either
# side when it is not GLCore before touching the known-good output.
LEGACY_INSTALL="$TMP_ROOT/legacy-install"
mkdir -p "$LEGACY_INSTALL/bin" "$LEGACY_INSTALL/lib"
ln -s "$SDK/bin/osgVerse_EarthExplorer" "$LEGACY_INSTALL/bin/osgVerse_EarthExplorer"
for directory in shaders skyboxes textures misc models; do
    ln -s "$SDK/$directory" "$LEGACY_INSTALL/$directory"
done
printf '%s\n' " compatibility" > "$LEGACY_INSTALL/lib/libosgVersePipeline.a"
expect_rejected_profile "Install SDK was not built for GLCore" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$LEGACY_INSTALL" OSG_RUNTIME_SDK="$RUNTIME_SDK" \
        OSGSOL_PACKAGE_OUTPUT="$APP" OSGSOL_PACKAGE_VERSION="$VERSION" \
        OSGSOL_BUILD_CHANNEL="$CHANNEL" OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" \
        OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

LEGACY_RUNTIME="$TMP_ROOT/legacy-runtime"
mkdir -p "$LEGACY_RUNTIME/include/osg" "$LEGACY_RUNTIME/lib/$PLUGVER"
printf '%s\n' '#define OSG_GL1_AVAILABLE' '#define OSG_GL2_AVAILABLE' \
    '/* #undef OSG_GL3_AVAILABLE */' > "$LEGACY_RUNTIME/include/osg/GL"
runtime_thread="$(find -L "$RUNTIME_SDK/lib" -maxdepth 1 \
    -name 'libOpenThreads*.dylib' -print -quit)"
runtime_plugin="$(find -L "$RUNTIME_SDK/lib/$PLUGVER" -maxdepth 1 \
    -name 'osgdb_*.so' -print -quit)"
test -n "$runtime_thread"
test -n "$runtime_plugin"
ln -s "$runtime_thread" "$LEGACY_RUNTIME/lib/$(basename "$runtime_thread")"
ln -s "$runtime_plugin" "$LEGACY_RUNTIME/lib/$PLUGVER/$(basename "$runtime_plugin")"
expect_rejected_profile "OSG runtime SDK is not GLCore" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$SDK" OSG_RUNTIME_SDK="$LEGACY_RUNTIME" \
        OSGSOL_PACKAGE_OUTPUT="$APP" OSGSOL_PACKAGE_VERSION="$VERSION" \
        OSGSOL_BUILD_CHANNEL="$CHANNEL" OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" \
        OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

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
        dependencies="$TMP_ROOT/dependencies"
        load_commands="$TMP_ROOT/load-commands"
        otool -L "$binary" > "$dependencies"
        otool -l "$binary" > "$load_commands"
        if tail -n +2 "$dependencies" | awk '{print $1}' |
           grep -F "$RUNTIME_SDK/" >/dev/null; then
            echo "FAIL: packaged Mach-O retains OSG runtime SDK path: $binary" >&2
            exit 1
        fi
        if tail -n +2 "$dependencies" | awk '{print $1}' |
           grep -F "$SDK/" >/dev/null; then
            echo "FAIL: packaged Mach-O retains install SDK path: $binary" >&2
            exit 1
        fi
        if tail -n +2 "$dependencies" | awk '{print $1}' |
           grep -E '^/(Users|private)/' >/dev/null; then
            echo "FAIL: packaged Mach-O retains a private dependency: $binary" >&2
            exit 1
        fi
        if awk '$1 == "cmd" && $2 == "LC_RPATH" { wanted = 1; next }
                wanted && $1 == "path" { print $2; wanted = 0 }' "$load_commands" |
           grep -E '^/' >/dev/null; then
            echo "FAIL: packaged Mach-O retains an absolute LC_RPATH: $binary" >&2
            exit 1
        fi
    fi
done < <(find "$APP/Contents" -type f -print0)

collect_osg_uuid_records "$RUNTIME_SDK/lib" | sort -u > "$TMP_ROOT/runtime-uuids"
collect_osg_uuid_records "$APP/Contents/lib" | sort -u > "$TMP_ROOT/package-uuids"
test -s "$TMP_ROOT/runtime-uuids"
test -s "$TMP_ROOT/package-uuids"
assert_unique_family_arch "$TMP_ROOT/runtime-uuids"
assert_unique_family_arch "$TMP_ROOT/package-uuids"
if ! cmp -s "$TMP_ROOT/runtime-uuids" "$TMP_ROOT/package-uuids"; then
    echo "FAIL: packaged OSG runtime UUIDs do not match the explicit runtime SDK" >&2
    diff -u "$TMP_ROOT/runtime-uuids" "$TMP_ROOT/package-uuids" >&2 || true
    exit 1
fi

# Mutation proof: the UUID gate must reject a second copy of any packaged Mach-O identity.
duplicate_source="$(find "$APP/Contents/lib" -type f -name 'libosg*.dylib' -print -quit)"
test -n "$duplicate_source"
mkdir -p "$APP/Contents/lib/task12-duplicate"
cp "$duplicate_source" "$APP/Contents/lib/task12-duplicate/$(basename "$duplicate_source")"
if audit_unique_macho_uuids "$APP"; then
    echo "FAIL: duplicate Mach-O UUID mutation was accepted" >&2
    exit 1
fi
rm -rf "$APP/Contents/lib/task12-duplicate"
audit_unique_macho_uuids "$APP"

env -u EARTH_AI_KEY -u OSG_LIBRARY_PATH HOME="$HOME_DIR" EARTH_IME=0 \
    EARTH_OFFSCREEN=1 EARTH_AUTOCAP=100 \
    "$APP/Contents/MacOS/osgSol_Earth" >"$LOG" 2>&1
grep -q "\[Earth\] offscreen context 1920x1080" "$LOG"
grep -q "\[Earth\] offscreen capture saved" "$LOG"
if grep -Eq "glCompileShader .* FAILED|glLinkProgram .* FAILED|version '.*' is not supported|OpenGL error '(invalid operation|invalid enumerant)'|after applying GLMode|after applying attribute (Material|LightModel)|Main earth scene is missing|Stack trace" "$LOG"; then
    echo "FAIL: packaged render smoke logged a shader/OpenGL failure" >&2
    exit 1
fi
test -s "$CAPTURE"
if find "$APP" -name imgui.ini -print -quit | grep -q .; then
    echo "FAIL: runtime wrote imgui.ini into signed bundle" >&2
    exit 1
fi
bash "$ROOT/tests/macos_normal_exit_tests.sh" "$APP"
codesign --verify --deep --strict "$APP"
echo "[OK] formal macOS staging identity, credential, smoke, and signature checks"
