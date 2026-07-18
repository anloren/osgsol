#!/bin/bash
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
    echo "SKIP: macOS packaging test"
    exit 0
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if grep -Eq '^[[:space:]]*xattr([[:space:]]|$)' "$ROOT/packaging/package_macos.sh"; then
    echo "FAIL: formal packaging must not invoke xattr" >&2
    exit 1
fi
SDK="${OSGVERSE_SDK:-$ROOT/build/sdk_core}"
RUNTIME_SDK="${OSG_RUNTIME_SDK:-}"
if [ -z "$RUNTIME_SDK" ]; then
    echo "FAIL: OSG_RUNTIME_SDK must be explicit for the packaging contract" >&2
    exit 64
fi
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

list_macho_dependencies()
{
    local binary="$1"
    otool -L "$binary" |
        sed -n '2,$ {
            s/^[[:space:]]*//
            s/[[:space:]]*(compatibility version.*$//
            p
        }'
}

make_install_fixture()
{
    local root="$1"
    local directory file
    mkdir -p "$root/bin" "$root/lib"
    ln -s "$SDK/bin/osgVerse_EarthExplorer" "$root/bin/osgVerse_EarthExplorer"
    cp "$SDK/lib/libosgVersePipeline.a" "$root/lib/libosgVersePipeline.a"
    for file in "$SDK/lib/"*.dylib "$SDK/lib/"*.so; do
        [ -e "$file" ] || continue
        cp -a "$file" "$root/lib/"
    done
    for directory in shaders skyboxes textures misc models; do
        mkdir -p "$root/$directory"
    done
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

assert_relative_dependency()
{
    local app="$1"
    local binary="$2"
    local dependency="$3"
    local base candidate resolved contents
    case "$dependency" in
        @rpath/*)
            base="$app/Contents/lib"
            candidate="$base/${dependency#@rpath/}"
            ;;
        @loader_path/*)
            base="$(dirname "$binary")"
            candidate="$base/${dependency#@loader_path/}"
            ;;
        @executable_path/*)
            base="$app/Contents/MacOS"
            candidate="$base/${dependency#@executable_path/}"
            ;;
        *)
            return 1
            ;;
    esac
    [ -f "$candidate" ] || return 1
    resolved="$(/bin/realpath "$candidate")"
    contents="$(/bin/realpath "$app/Contents")"
    case "$resolved" in
        "$contents"/*)
            file -b "$resolved" | grep -q 'Mach-O'
            ;;
        *)
            return 1
            ;;
    esac
}

assert_bundle_dependency_closure()
{
    local app="$1"
    local binary dependency dependencies load_commands dylib_id
    dependencies="$TMP_ROOT/closure-dependencies"
    load_commands="$TMP_ROOT/closure-load-commands"
    while IFS= read -r -d '' binary; do
        if ! file -b "$binary" | grep -q 'Mach-O'; then
            continue
        fi
        codesign --verify --strict "$binary"
        dylib_id="$(otool -D "$binary" | sed -n '2p')"
        list_macho_dependencies "$binary" > "$dependencies"
        while IFS= read -r dependency; do
            if [ -n "$dylib_id" ] && [ "$dependency" = "$dylib_id" ]; then
                continue
            fi
            case "$dependency" in
                /System/Library/*|/usr/lib/*)
                    ;;
                @rpath/*|@loader_path/*|@executable_path/*)
                    if ! assert_relative_dependency "$app" "$binary" "$dependency"; then
                        echo "FAIL: unresolved packaged relative dependency: $dependency in $binary" >&2
                        return 1
                    fi
                    ;;
                *)
                    echo "FAIL: packaged Mach-O retains a non-system dependency: $dependency in $binary" >&2
                    return 1
                    ;;
            esac
        done < "$dependencies"
        otool -l "$binary" > "$load_commands"
        if awk '$1 == "cmd" && $2 == "LC_RPATH" { wanted = 1; next }
                wanted && $1 == "path" { print $2; wanted = 0 }' "$load_commands" |
           grep -E '^/' >/dev/null; then
            echo "FAIL: packaged Mach-O retains an absolute LC_RPATH: $binary" >&2
            return 1
        fi
    done < <(find "$app/Contents" -type f -print0)
}

# A rejected invocation must leave an existing output untouched.
mkdir -p "$APP"
printf '%s\n' "known-good" > "$APP/keep"
if env -u EARTH_AI_KEY -u OSG_RUNTIME_SDK \
   OSG_ROOT="$RUNTIME_SDK" OSGVERSE_SDK="$SDK" \
   OSGSOL_PACKAGE_OUTPUT="$APP" OSGSOL_PACKAGE_VERSION="$VERSION" \
   OSGSOL_BUILD_CHANNEL="$CHANNEL" OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" \
   OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
   OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
   bash "$ROOT/packaging/package_macos.sh" >"$LOG" 2>&1; then
    echo "FAIL: packaging accepted inherited OSG_ROOT without explicit OSG_RUNTIME_SDK" >&2
    exit 1
fi
grep -q "OSG_RUNTIME_SDK must explicitly select" "$LOG"
grep -q "known-good" "$APP/keep"

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

expect_rejected_profile "Source commit is not a commit in this repository" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$SDK" OSG_RUNTIME_SDK="$RUNTIME_SDK" \
        OSGSOL_PACKAGE_OUTPUT="$APP" OSGSOL_PACKAGE_VERSION="$VERSION" \
        OSGSOL_BUILD_CHANNEL="$CHANNEL" \
        OSGSOL_SOURCE_COMMIT="0000000000000000000000000000000000000000" \
        OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

ANNOTATED_TAG_OBJECT="$(git -C "$ROOT" for-each-ref \
    --format='%(objecttype) %(objectname)' refs/tags |
    awk '$1 == "tag" { print $2; exit }')"
test -n "$ANNOTATED_TAG_OBJECT"
expect_rejected_profile "Source commit is not a commit in this repository" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$SDK" OSG_RUNTIME_SDK="$RUNTIME_SDK" \
        OSGSOL_PACKAGE_OUTPUT="$APP" OSGSOL_PACKAGE_VERSION="$VERSION" \
        OSGSOL_BUILD_CHANNEL="$CHANNEL" OSGSOL_SOURCE_COMMIT="$ANNOTATED_TAG_OBJECT" \
        OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

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

# A generic private dependency must be rejected by the production pre-publication audit, not only
# by this test's post-package inspection.
PRIVATE_INSTALL="$TMP_ROOT/private-install"
make_install_fixture "$PRIVATE_INSTALL"
private_source="$(find "$PRIVATE_INSTALL/lib" -maxdepth 1 -type f -name '*.so' -print -quit)"
test -n "$private_source"
private_probe="$private_source"
private_dependency="$(otool -L "$private_probe" |
    awk 'NR > 1 && $1 ~ "^/(usr|System)/" { print $1; exit }')"
test -n "$private_dependency"
install_name_tool -change "$private_dependency" \
    "/private/osgsol-task12/libtask12-missing.dylib" "$private_probe"
expect_rejected_profile "Missing non-system dependency" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$PRIVATE_INSTALL" \
        OSG_RUNTIME_SDK="$RUNTIME_SDK" OSGSOL_PACKAGE_OUTPUT="$APP" \
        OSGSOL_PACKAGE_VERSION="$VERSION" OSGSOL_BUILD_CHANNEL="$CHANNEL" \
        OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

PRIVATE_ID_INSTALL="$TMP_ROOT/private-id-install"
make_install_fixture "$PRIVATE_ID_INSTALL"
private_id_probe="$(find "$PRIVATE_ID_INSTALL/lib" -maxdepth 1 -type f -name '*.so' -print -quit)"
test -n "$private_id_probe"
install_name_tool -id "/private/osgsol-task12/libtask12-private-id.dylib" "$private_id_probe"
expect_rejected_profile "Private dependency remains in packaged binary" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$PRIVATE_ID_INSTALL" \
        OSG_RUNTIME_SDK="$RUNTIME_SDK" OSGSOL_PACKAGE_OUTPUT="$APP" \
        OSGSOL_PACKAGE_VERSION="$VERSION" OSGSOL_BUILD_CHANNEL="$CHANNEL" \
        OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

# A second file with an existing Mach-O UUID must be rejected by the production audit before the
# known-good output is replaced.
DUPLICATE_INSTALL="$TMP_ROOT/duplicate-install"
make_install_fixture "$DUPLICATE_INSTALL"
duplicate_fixture_source="$(find "$RUNTIME_SDK/lib" -maxdepth 1 -type f \
    -name 'libosg*.dylib' -print -quit)"
test -n "$duplicate_fixture_source"
cp "$duplicate_fixture_source" "$DUPLICATE_INSTALL/lib/task12-duplicate-uuid.dylib"
expect_rejected_profile "Duplicate Mach-O UUID in packaged bundle" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$DUPLICATE_INSTALL" \
        OSG_RUNTIME_SDK="$RUNTIME_SDK" OSGSOL_PACKAGE_OUTPUT="$APP" \
        OSGSOL_PACKAGE_VERSION="$VERSION" OSGSOL_BUILD_CHANNEL="$CHANNEL" \
        OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

# Bundle-relative dependencies must resolve to Mach-O files inside Contents. Cover both token
# families and preserve the known-good output on missing and escaping paths.
LOADER_MISSING_INSTALL="$TMP_ROOT/loader-missing-install"
make_install_fixture "$LOADER_MISSING_INSTALL"
loader_missing_probe="$(find "$LOADER_MISSING_INSTALL/lib" -maxdepth 1 -type f \
    -name '*.so' -print -quit)"
loader_missing_dependency="$(otool -L "$loader_missing_probe" |
    awk 'NR > 1 && $1 ~ "^/(usr|System)/" { print $1; exit }')"
test -n "$loader_missing_dependency"
install_name_tool -change "$loader_missing_dependency" \
    '@loader_path/task12-missing-loader.dylib' "$loader_missing_probe"
expect_rejected_profile "Missing bundle-token dependency" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$LOADER_MISSING_INSTALL" \
        OSG_RUNTIME_SDK="$RUNTIME_SDK" OSGSOL_PACKAGE_OUTPUT="$APP" \
        OSGSOL_PACKAGE_VERSION="$VERSION" OSGSOL_BUILD_CHANNEL="$CHANNEL" \
        OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

EXECUTABLE_ESCAPE_INSTALL="$TMP_ROOT/executable-escape-install"
make_install_fixture "$EXECUTABLE_ESCAPE_INSTALL"
executable_escape_probe="$(find "$EXECUTABLE_ESCAPE_INSTALL/lib" -maxdepth 1 -type f \
    -name '*.so' -print -quit)"
executable_escape_dependency="$(otool -L "$executable_escape_probe" |
    awk 'NR > 1 && $1 ~ "^/(usr|System)/" { print $1; exit }')"
test -n "$executable_escape_dependency"
printf '%s\n' 'int task12_escape(void) { return 12; }' > "$TMP_ROOT/task12-escape.c"
clang -dynamiclib "$TMP_ROOT/task12-escape.c" -o "$TMP_ROOT/task12-escape.dylib"
install_name_tool -change "$executable_escape_dependency" \
    '@executable_path/../../../task12-escape.dylib' "$executable_escape_probe"
expect_rejected_profile "bundle-token dependency escapes package" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$EXECUTABLE_ESCAPE_INSTALL" \
        OSG_RUNTIME_SDK="$RUNTIME_SDK" OSGSOL_PACKAGE_OUTPUT="$APP" \
        OSGSOL_PACKAGE_VERSION="$VERSION" OSGSOL_BUILD_CHANNEL="$CHANNEL" \
        OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

RPATH_ESCAPE_INSTALL="$TMP_ROOT/rpath-escape-install"
make_install_fixture "$RPATH_ESCAPE_INSTALL"
rpath_escape_probe="$(find "$RPATH_ESCAPE_INSTALL/lib" -maxdepth 1 -type f \
    -name '*.so' -print -quit)"
rpath_escape_dependency="$(otool -L "$rpath_escape_probe" |
    awk 'NR > 1 && $1 ~ "^/(usr|System)/" { print $1; exit }')"
test -n "$rpath_escape_dependency"
printf '%s\n' 'int task12_rpath_escape(void) { return 12; }' > \
    "$TMP_ROOT/task12-rpath-escape.c"
clang -dynamiclib "$TMP_ROOT/task12-rpath-escape.c" \
    -o "$TMP_ROOT/task12-rpath-escape.dylib"
ln -s "$TMP_ROOT/task12-rpath-escape.dylib" \
    "$RPATH_ESCAPE_INSTALL/lib/task12-rpath-escape.dylib"
install_name_tool -change "$rpath_escape_dependency" \
    '@rpath/task12-rpath-escape.dylib' "$rpath_escape_probe"
expect_rejected_profile "bundled runtime dependency escapes package" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$RPATH_ESCAPE_INSTALL" \
        OSG_RUNTIME_SDK="$RUNTIME_SDK" OSGSOL_PACKAGE_OUTPUT="$APP" \
        OSGSOL_PACKAGE_VERSION="$VERSION" OSGSOL_BUILD_CHANNEL="$CHANNEL" \
        OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

# Two distinct external sources may not silently collapse onto one bundle-local basename.
COLLISION_INSTALL="$TMP_ROOT/collision-install"
COLLISION_SOURCE="$TMP_ROOT/collision-source"
make_install_fixture "$COLLISION_INSTALL"
mkdir -p "$COLLISION_SOURCE"
collision_probe="$(find "$COLLISION_INSTALL/lib" -maxdepth 1 -type f -name '*.so' -print -quit)"
collision_dependency="$(otool -L "$collision_probe" |
    awk 'NR > 1 && $1 ~ "^/(usr|System)/" { print $1; exit }')"
collision_existing="$(find "$COLLISION_INSTALL/lib" -maxdepth 1 -type f \
    \( -name 'libosg*.dylib' -o -name 'libosg*.so' \) -print -quit)"
test -n "$collision_dependency"
test -n "$collision_existing"
collision_external="$COLLISION_SOURCE/$(basename "$collision_existing")"
printf '%s\n' 'int task12_collision(void) { return 12; }' > "$COLLISION_SOURCE/collision.c"
clang -dynamiclib "$COLLISION_SOURCE/collision.c" \
    -Wl,-install_name,"$collision_external" -o "$collision_external"
install_name_tool -change "$collision_dependency" "$collision_external" "$collision_probe"
expect_rejected_profile "Dependency basename collision" \
    env -u EARTH_AI_KEY OSGVERSE_SDK="$COLLISION_INSTALL" \
        OSG_RUNTIME_SDK="$RUNTIME_SDK" OSGSOL_PACKAGE_OUTPUT="$APP" \
        OSGSOL_PACKAGE_VERSION="$VERSION" OSGSOL_BUILD_CHANNEL="$CHANNEL" \
        OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
        OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
        bash "$ROOT/packaging/package_macos.sh"

# A formal package must recursively close non-system dependencies instead of retaining paths to
# the build host. Build a three-library chain entirely inside this test: the install SDK contains
# only the root, while its middle and leaf dependencies live outside the SDK.
EXTERNAL_INSTALL="$TMP_ROOT/external-install"
# Keep the source closure in a directory with a space so dependency parsing is verified against
# normal macOS paths rather than only whitespace-free build roots.
EXTERNAL_SOURCE="$TMP_ROOT/external source"
EXTERNAL_OUTPUT="$TMP_ROOT/external-output/osgSol Earth.app"
make_install_fixture "$EXTERNAL_INSTALL"
mkdir -p "$EXTERNAL_SOURCE" "$(dirname "$EXTERNAL_OUTPUT")"
printf '%s\n' 'int task12_leaf(void) { return 12; }' > "$EXTERNAL_SOURCE/leaf.c"
clang -dynamiclib "$EXTERNAL_SOURCE/leaf.c" \
    -Wl,-install_name,"$EXTERNAL_SOURCE/libtask12-leaf.dylib" \
    -o "$EXTERNAL_SOURCE/libtask12-leaf.dylib"
printf '%s\n' \
    'extern int task12_leaf(void);' \
    'int task12_middle(void) { return task12_leaf(); }' > "$EXTERNAL_SOURCE/middle.c"
clang -dynamiclib "$EXTERNAL_SOURCE/middle.c" "$EXTERNAL_SOURCE/libtask12-leaf.dylib" \
    -Wl,-install_name,"$EXTERNAL_SOURCE/libtask12-middle.dylib" \
    -o "$EXTERNAL_SOURCE/libtask12-middle.dylib"
printf '%s\n' \
    'extern int task12_middle(void);' \
    'int task12_root(void) { return task12_middle(); }' > "$EXTERNAL_SOURCE/root.c"
clang -dynamiclib "$EXTERNAL_SOURCE/root.c" "$EXTERNAL_SOURCE/libtask12-middle.dylib" \
    -Wl,-install_name,@rpath/libtask12-root.dylib \
    -o "$EXTERNAL_INSTALL/lib/libtask12-root.dylib"
: > "$LOG"
env -u EARTH_AI_KEY OSGVERSE_SDK="$EXTERNAL_INSTALL" \
    OSG_RUNTIME_SDK="$RUNTIME_SDK" OSGSOL_PACKAGE_OUTPUT="$EXTERNAL_OUTPUT" \
    OSGSOL_PACKAGE_VERSION="$VERSION" OSGSOL_BUILD_CHANNEL="$CHANNEL" \
    OSGSOL_SOURCE_COMMIT="$SOURCE_COMMIT" OSGSOL_PACKAGE_EXECUTABLE="osgSol_Earth" \
    OSGSOL_ALPHAEARTH_INDEX="$ALPHAEARTH_INDEX" \
    bash "$ROOT/packaging/package_macos.sh"
if [ ! -f "$EXTERNAL_OUTPUT/Contents/lib/libtask12-middle.dylib" ]; then
    echo "FAIL: direct non-system dependency was not copied into the package" >&2
    exit 1
fi
if [ ! -f "$EXTERNAL_OUTPUT/Contents/lib/libtask12-leaf.dylib" ]; then
    echo "FAIL: transitive non-system dependency was not copied into the package" >&2
    exit 1
fi
if ! otool -L "$EXTERNAL_OUTPUT/Contents/lib/libtask12-root.dylib" |
   sed -n '2,$ { s/^[[:space:]]*//; s/[[:space:]]*(compatibility version.*$//; p; }' |
   grep -qx '@rpath/libtask12-middle.dylib'; then
    echo "FAIL: direct non-system dependency was not closed as @rpath" >&2
    exit 1
fi
if ! otool -L "$EXTERNAL_OUTPUT/Contents/lib/libtask12-middle.dylib" |
   sed -n '2,$ { s/^[[:space:]]*//; s/[[:space:]]*(compatibility version.*$//; p; }' |
   grep -qx '@rpath/libtask12-leaf.dylib'; then
    echo "FAIL: transitive non-system dependency was not closed as @rpath" >&2
    exit 1
fi
assert_bundle_dependency_closure "$EXTERNAL_OUTPUT"

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
        list_macho_dependencies "$binary" > "$dependencies"
        otool -l "$binary" > "$load_commands"
        if grep -F "$RUNTIME_SDK/" "$dependencies" >/dev/null; then
            echo "FAIL: packaged Mach-O retains OSG runtime SDK path: $binary" >&2
            exit 1
        fi
        if grep -F "$SDK/" "$dependencies" >/dev/null; then
            echo "FAIL: packaged Mach-O retains install SDK path: $binary" >&2
            exit 1
        fi
        if grep -E '^/(Users|private)/' "$dependencies" >/dev/null; then
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
assert_bundle_dependency_closure "$APP"
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

if [ "${OSGSOL_PACKAGE_TEST_SKIP_RUNTIME_SMOKE:-0}" = "1" ]; then
    codesign --verify --deep --strict "$APP"
    echo "[OK] formal macOS staging identity, provenance, closure, and signature checks"
    exit 0
fi

env -u EARTH_AI_KEY -u OSG_LIBRARY_PATH HOME="$HOME_DIR" EARTH_IME=0 \
    EARTH_OFFSCREEN=1 EARTH_AUTOCAP=100 EARTH_PREFETCH=0 \
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
