#!/bin/bash
# Package an installed EarthExplorer tree as the formal osgSol Earth product.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${OSGVERSE_SDK:-$REPO/build/sdk_core}"
OSG_RUNTIME_SDK="${OSG_RUNTIME_SDK:-${OSG_ROOT:-}}"
APP="${OSGSOL_PACKAGE_OUTPUT:-$REPO/dist/osgSol Earth.app}"
VERSION="${OSGSOL_PACKAGE_VERSION:-0.3.0}"
BUILD_CHANNEL="${OSGSOL_BUILD_CHANNEL:-developer}"
SOURCE_COMMIT="${OSGSOL_SOURCE_COMMIT:-$(git -C "$REPO" rev-parse HEAD)}"
PRODUCT_EXECUTABLE="${OSGSOL_PACKAGE_EXECUTABLE:-osgSol_Earth}"
SOURCE_EXECUTABLE="osgVerse_EarthExplorer"
PLUGVER="osgPlugins-3.6.5"
ALPHAEARTH_INDEX="${OSGSOL_ALPHAEARTH_INDEX:-$SDK/misc/science/alphaearth/alphaearth.sqlite}"

fail()
{
    local code="$1"
    shift
    echo "[error] $*" >&2
    exit "$code"
}

# Reject every invalid invocation before creating, deleting, or moving output.
if [ -n "${EARTH_AI_KEY:-}" ]; then
    fail 64 "Refusing to package while EARTH_AI_KEY is set; unset it and use per-user configuration."
fi
if [ "$(basename "$APP")" != "osgSol Earth.app" ]; then
    fail 68 "Package output must use the fixed product name osgSol Earth.app"
fi
if [ "$APP" = "${HOME}/Desktop/osgSol Earth.app" ]; then
    fail 68 "Refusing to package directly over the fixed Desktop app; use a staging path"
fi
if [[ ! "$VERSION" =~ ^[0-9A-Za-z][0-9A-Za-z.+-]*$ ]]; then
    fail 68 "Invalid package version: $VERSION"
fi
if [[ ! "$BUILD_CHANNEL" =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]]; then
    fail 68 "Invalid build channel: $BUILD_CHANNEL"
fi
if [[ ! "$SOURCE_COMMIT" =~ ^[0-9a-f]{40}$ ]]; then
    fail 68 "Source commit must be an exact 40-character lowercase Git object id"
fi
if [[ ! "$PRODUCT_EXECUTABLE" =~ ^[0-9A-Za-z._-]+$ ]]; then
    fail 68 "Invalid product executable name: $PRODUCT_EXECUTABLE"
fi
if [ ! -x "$SDK/bin/$SOURCE_EXECUTABLE" ]; then
    fail 66 "Install SDK is incomplete: $SDK"
fi
if [ ! -f "$ALPHAEARTH_INDEX" ]; then
    fail 66 "AlphaEarth index is required for the formal package: $ALPHAEARTH_INDEX"
fi
ALPHAEARTH_INDEX_SHA256="$(shasum -a 256 "$ALPHAEARTH_INDEX" | awk '{print $1}')"
for directory in shaders skyboxes textures misc models; do
    if [ ! -d "$SDK/$directory" ]; then
        fail 66 "Install SDK is incomplete: missing $SDK/$directory"
    fi
done
if [ ! -d "$SDK/lib" ]; then
    fail 66 "Install SDK is incomplete: missing $SDK/lib"
fi
if [ -z "$OSG_RUNTIME_SDK" ]; then
    fail 66 "OSG_RUNTIME_SDK must explicitly select the GLCore runtime used to build the app"
fi
if [ ! -d "$OSG_RUNTIME_SDK/lib" ] ||
   [ -z "$(find "$OSG_RUNTIME_SDK/lib" -maxdepth 1 -name 'libOpenThreads*.dylib' -print -quit)" ] ||
   [ ! -d "$OSG_RUNTIME_SDK/lib/$PLUGVER" ] ||
   [ -z "$(find -L "$OSG_RUNTIME_SDK/lib/$PLUGVER" -maxdepth 1 -name 'osgdb_*.so' -print -quit)" ]; then
    fail 66 "OSG runtime SDK is incomplete: $OSG_RUNTIME_SDK"
fi

# The macOS headless and foreground render paths require the same GLCore OSG profile at compile
# time and runtime. The OSG ABI/install names alone cannot distinguish a legacy build, so validate
# both independent inputs before creating or moving any output bundle.
RUNTIME_GL_HEADER="$OSG_RUNTIME_SDK/include/osg/GL"
if [ ! -f "$RUNTIME_GL_HEADER" ]; then
    fail 66 "OSG runtime SDK is not GLCore: missing $RUNTIME_GL_HEADER"
fi
RUNTIME_GL3_COUNT="$(awk \
    '$1 == "#define" && $2 == "OSG_GL3_AVAILABLE" { count++ } END { print count + 0 }' \
    "$RUNTIME_GL_HEADER")"
RUNTIME_LEGACY_COUNT="$(awk '
    $1 == "#define" && ($2 == "OSG_GL1_AVAILABLE" ||
                        $2 == "OSG_GL2_AVAILABLE" ||
                        $2 == "OSG_GL_MATRICES_AVAILABLE" ||
                        $2 == "OSG_GL_FIXED_FUNCTION_AVAILABLE") { count++ }
    END { print count + 0 }
' "$RUNTIME_GL_HEADER")"
if [ "$RUNTIME_GL3_COUNT" -ne 1 ] || [ "$RUNTIME_LEGACY_COUNT" -ne 0 ]; then
    fail 66 "OSG runtime SDK is not GLCore (GL3=$RUNTIME_GL3_COUNT legacy=$RUNTIME_LEGACY_COUNT)"
fi

PROFILE_ARCHIVE="$SDK/lib/libosgVersePipeline.a"
if [ ! -f "$PROFILE_ARCHIVE" ]; then
    fail 66 "Install SDK was not built for GLCore: missing $PROFILE_ARCHIVE"
fi
COMPILE_GL3_COUNT="$(strings -a "$PROFILE_ARCHIVE" |
    awk '$0 == "#define VERSE_GLES3 1" { count++ } END { print count + 0 }')"
COMPILE_CORE_SUFFIX_COUNT="$(strings -a "$PROFILE_ARCHIVE" |
    awk '$0 == " core" { count++ } END { print count + 0 }')"
COMPILE_LEGACY_SUFFIX_COUNT="$(strings -a "$PROFILE_ARCHIVE" |
    awk '$0 == " compatibility" { count++ } END { print count + 0 }')"
if [ "$COMPILE_GL3_COUNT" -lt 1 ] ||
   [ "$COMPILE_CORE_SUFFIX_COUNT" -lt 1 ] ||
   [ "$COMPILE_LEGACY_SUFFIX_COUNT" -ne 0 ]; then
    fail 66 "Install SDK was not built for GLCore (GL3=$COMPILE_GL3_COUNT core=$COMPILE_CORE_SUFFIX_COUNT legacy=$COMPILE_LEGACY_SUFFIX_COUNT)"
fi

APP_PARENT="$(dirname "$APP")"
mkdir -p "$APP_PARENT"
APP_PARENT="$(cd "$APP_PARENT" && pwd)"
APP="$APP_PARENT/osgSol Earth.app"
BUILD_APP="$APP_PARENT/.osgSol Earth.app.packaging.$$"
PREVIOUS_APP="$APP_PARENT/.osgSol Earth.app.previous.$$"

cleanup()
{
    rm -rf "$BUILD_APP"
    if [ -e "$PREVIOUS_APP" ] && [ ! -e "$APP" ]; then
        mv "$PREVIOUS_APP" "$APP"
    fi
}
trap cleanup EXIT

rm -rf "$BUILD_APP" "$PREVIOUS_APP"
mkdir -p "$BUILD_APP/Contents/MacOS"
mkdir -p "$BUILD_APP/Contents/lib/$PLUGVER"
mkdir -p "$BUILD_APP/Contents/bin"

# Executable and all runtime libraries come only from the explicit install/runtime trees.
cp "$SDK/bin/$SOURCE_EXECUTABLE" "$BUILD_APP/Contents/MacOS/$PRODUCT_EXECUTABLE"
for lib_root in "$OSG_RUNTIME_SDK/lib" "$SDK/lib"; do
    for file in "$lib_root/"*.dylib "$lib_root/"*.so; do
        [ -e "$file" ] || continue
        cp -a "$file" "$BUILD_APP/Contents/lib/"
    done
done

for plugin_root in "$OSG_RUNTIME_SDK/lib/$PLUGVER" "$SDK/lib/$PLUGVER"; do
    for file in "$plugin_root/"*.so; do
        [ -e "$file" ] || continue
        cp -a "$file" "$BUILD_APP/Contents/lib/$PLUGVER/"
    done
done
ln -s "../lib/$PLUGVER" "$BUILD_APP/Contents/bin/$PLUGVER"

for directory in shaders skyboxes textures misc models; do
    cp -a "$SDK/$directory" "$BUILD_APP/Contents/$directory"
done
mkdir -p "$BUILD_APP/Contents/misc/science/alphaearth"
cp "$ALPHAEARTH_INDEX" \
    "$BUILD_APP/Contents/misc/science/alphaearth/alphaearth.sqlite"

# Make every packaged Mach-O resolve only against its bundle-local runtime closure.
install_name_tool -delete_rpath '$ORIGIN:$ORIGIN/../lib' \
    "$BUILD_APP/Contents/MacOS/$PRODUCT_EXECUTABLE" 2>/dev/null || true
install_name_tool -delete_rpath '@executable_path/../lib' \
    "$BUILD_APP/Contents/MacOS/$PRODUCT_EXECUTABLE" 2>/dev/null || true
install_name_tool -add_rpath '@executable_path/../lib' \
    "$BUILD_APP/Contents/MacOS/$PRODUCT_EXECUTABLE"

for file in "$BUILD_APP/Contents/lib/"*.dylib "$BUILD_APP/Contents/lib/"*.so; do
    [ -f "$file" ] || continue
    install_name_tool -delete_rpath '$ORIGIN:$ORIGIN/../lib' "$file" 2>/dev/null || true
    install_name_tool -delete_rpath "$OSG_RUNTIME_SDK/lib" "$file" 2>/dev/null || true
    install_name_tool -delete_rpath "$SDK/lib" "$file" 2>/dev/null || true
    install_name_tool -delete_rpath '@loader_path' "$file" 2>/dev/null || true
    install_name_tool -add_rpath '@loader_path' "$file" 2>/dev/null || true
done

for file in "$BUILD_APP/Contents/lib/$PLUGVER/"*.so; do
    [ -f "$file" ] || continue
    install_name_tool -delete_rpath '$ORIGIN:$ORIGIN/../lib' "$file" 2>/dev/null || true
    install_name_tool -delete_rpath "$OSG_RUNTIME_SDK/lib" "$file" 2>/dev/null || true
    install_name_tool -delete_rpath "$SDK/lib" "$file" 2>/dev/null || true
    install_name_tool -delete_rpath '@loader_path/..' "$file" 2>/dev/null || true
    install_name_tool -add_rpath '@loader_path/..' "$file" 2>/dev/null || true
done

# A runtime SDK may be reached through a symlink while its binaries retain the real build path.
# Remove every absolute LC_RPATH; the bundle-local @loader_path/@executable_path entries above are
# the complete runtime search policy for the packaged application.
while IFS= read -r -d '' binary; do
    if ! file -b "$binary" | grep -q 'Mach-O'; then
        continue
    fi
    while IFS= read -r rpath; do
        case "$rpath" in
            /*)
                install_name_tool -delete_rpath "$rpath" "$binary"
                ;;
        esac
    done < <(otool -l "$binary" |
        awk '$1 == "cmd" && $2 == "LC_RPATH" { wanted = 1; next }
             wanted && $1 == "path" { print $2; wanted = 0 }')
done < <(find "$BUILD_APP/Contents" -type f -print0)

# A fresh CMake build may encode absolute Homebrew OSG paths while osgVerse libraries use @rpath.
# Loading both identities creates two OSG runtimes in one process and corrupts GL dispatch. Rewrite
# every dependency whose basename is already bundled to the single bundle-local @rpath identity.
while IFS= read -r -d '' binary; do
    if ! file -b "$binary" | grep -q 'Mach-O'; then
        continue
    fi
    dylib_id="$(otool -D "$binary" 2>/dev/null | sed -n '2p')"
    if [ -n "$dylib_id" ]; then
        dylib_id_name="$(basename "$dylib_id")"
        if [ -e "$BUILD_APP/Contents/lib/$dylib_id_name" ] &&
           [ "$dylib_id" != "@rpath/$dylib_id_name" ]; then
            install_name_tool -id "@rpath/$dylib_id_name" "$binary" 2>/dev/null
        fi
    fi
    while IFS= read -r dependency; do
        dependency_name="$(basename "$dependency")"
        if [ -e "$BUILD_APP/Contents/lib/$dependency_name" ] &&
           [ "$dependency" != "@rpath/$dependency_name" ]; then
            install_name_tool -change "$dependency" "@rpath/$dependency_name" "$binary" \
                2>/dev/null
        fi
    done < <(otool -L "$binary" | awk 'NR > 1 {print $1}')
done < <(find "$BUILD_APP/Contents" -type f -print0)

# Every relocated @rpath edge must resolve and no packaged Mach-O may retain an install-tree or
# OSG-runtime-tree path. Python remains a separately recorded clean-machine distribution debt.
while IFS= read -r -d '' binary; do
    if ! file -b "$binary" | grep -q 'Mach-O'; then
        continue
    fi
    dylib_id="$(otool -D "$binary" 2>/dev/null | sed -n '2p')"
    if otool -l "$binary" |
       awk '$1 == "cmd" && $2 == "LC_RPATH" { wanted = 1; next }
            wanted && $1 == "path" { print $2; wanted = 0 }' |
       grep -E '^/' >/dev/null; then
        fail 67 "Absolute LC_RPATH remains in packaged binary: $binary"
    fi
    while IFS= read -r dependency; do
        if [ -n "$dylib_id" ] && [ "$dependency" = "$dylib_id" ]; then
            continue
        fi
        case "$dependency" in
            @rpath/*)
                if [ ! -e "$BUILD_APP/Contents/lib/${dependency#@rpath/}" ]; then
                    fail 67 "Missing bundled runtime dependency: $dependency in $binary"
                fi
                ;;
        esac
        if [[ "$dependency" == "$SDK"/* ]] ||
           [[ "$dependency" == "$OSG_RUNTIME_SDK"/* ]]; then
            fail 67 "Unrelocated SDK dependency: $dependency in $binary"
        fi
    done < <(otool -L "$binary" | awk 'NR > 1 {print $1}')
done < <(find "$BUILD_APP/Contents" -type f -print0)

cat > "$BUILD_APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>osgSol Earth</string>
  <key>CFBundleDisplayName</key><string>osgSol Earth</string>
  <key>CFBundleIdentifier</key><string>com.anloren.osgsol.earth</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>$PRODUCT_EXECUTABLE</string>
  <key>ScienceEarthBuildChannel</key><string>$BUILD_CHANNEL</string>
  <key>ScienceEarthSourceCommit</key><string>$SOURCE_COMMIT</string>
  <key>ScienceEarthIndexSha256</key><string>$ALPHAEARTH_INDEX_SHA256</string>
  <key>NSHighResolutionCapable</key><false/>
  <key>NSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
PLIST

if find "$BUILD_APP" -name imgui.ini -print -quit | grep -q .; then
    fail 65 "Refusing to sign a bundle containing imgui.ini"
fi

# Copied Finder/resource metadata invalidates strict signatures. Clean the complete tree first.
chmod -R u+w "$BUILD_APP"
xattr -cr "$BUILD_APP"

# install_name_tool invalidates upstream signatures. Sign every loose Mach-O explicitly because
# codesign --deep does not reliably discover dylibs/plugins that are not nested bundles.
while IFS= read -r -d '' binary; do
    if file -b "$binary" | grep -q 'Mach-O'; then
        codesign --force --sign - "$binary" 2>/dev/null
        codesign --verify --strict "$binary"
    fi
done < <(find "$BUILD_APP/Contents" -type f -print0)

codesign --force --deep --sign - "$BUILD_APP"
codesign --verify --deep --strict "$BUILD_APP"

# Publish only the already verified staging bundle. A failed build leaves the previous output intact.
if [ -e "$APP" ]; then
    mv "$APP" "$PREVIOUS_APP"
fi
mv "$BUILD_APP" "$APP"
rm -rf "$PREVIOUS_APP"
trap - EXIT

echo "Built and verified staging bundle: $APP"
