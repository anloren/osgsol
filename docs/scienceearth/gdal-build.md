# ScienceEarth private GDAL dependency prefix

This recipe builds a private, static macOS dependency prefix for the optional ScienceEarth
provider. It does not install or copy a Homebrew GDAL tree and never writes below
`/opt/homebrew` or `/usr/local`. The only platform libraries used are curl and SQLite from the
active macOS SDK.

## Pinned source archives

The URLs are the projects' official release archives. The byte counts and SHA-256 values below
were verified on 2026-07-12 by `--download-only`; their total is 24,125,929 bytes (24.12 MB
decimal).

| Component | Official source URL | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| GDAL 3.13.1 | `https://github.com/OSGeo/gdal/releases/download/v3.13.1/gdal-3.13.1.tar.gz` | 15,709,136 | `e04e9813bd215b56753d5554330c53be25f3df2d7ed7e6413a19e6b66751c675` |
| PROJ 9.8.1 | `https://download.osgeo.org/proj/proj-9.8.1.tar.gz` | 5,981,846 | `af5b731c145c1d13c4e3b4eeb7d167e94e845e440f71e3496b4ed8dae0291960` |
| ZSTD 1.5.7 | `https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz` | 2,434,947 | `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3` |

The authoritative pins are in `packaging/science_deps/versions.env` and
`packaging/science_deps/checksums.txt`. The builder requires those two files to agree exactly
with the three archive names and hashes before it reads an archive. It also verifies and applies
the pinned `gdal-3.13.1-disable-shapelib.patch` (SHA-256
`f7a2824634fdf6ed1ce1bd53b685a6e4f7053793c295f4f34e996e8a19546040`). The patch disables
the internal Shapelib fallback when the Shape driver is off and lets GDAL's own C23 `#embed`
compile test recognize AppleClang. The private build additionally verifies and applies the
relocatable-resource patch (SHA-256
`17741b49beeb10a4f6663e0e3197807e7a3daa84610d4c092ddc9c6e81b9fa79`) and the opt-in
parallel HEAD/Range prototype patch (SHA-256
`d545c492eda3a7c9c7faa4ed06334dfd0723c50aa99ca5f62cb6c7ae664255e8`). The latter remains
path-specific and off by default; it is not part of the protected Desktop runtime.

## Rebuild commands

Run from the repository root:

```bash
bash packaging/science_deps/build_science_deps.sh --download-only
bash packaging/science_deps/build_science_deps.sh --build --jobs 4
bash packaging/science_deps/build_science_deps.sh --verify
du -sh build/science-deps/prefix
find build/science-deps/prefix -type f | sort
```

The default layout is:

```text
build/science-deps/downloads
build/science-deps/src
build/science-deps/build
build/science-deps/prefix
```

`SCIENCE_DEPS_ROOT`, `SCIENCE_DEPS_DOWNLOADS`, `SCIENCE_DEPS_SRC`,
`SCIENCE_DEPS_BUILD`, and `SCIENCE_DEPS_PREFIX` can relocate those directories, subject to the
same safety contract. The canonical root must be a strict descendant of this repository's
`build` directory; every mutable child must be a distinct strict descendant of that root and may
not overlap another child. Nonempty override directories require the builder's repository-bound
`.science-deps-owned` marker. Every recursive reset rechecks canonical containment, symlinks,
and that marker before deletion. A build cleanly re-extracts every verified archive rather than
reusing an old source tree.

The builder therefore refuses repository, home, system, `/opt/homebrew`, `/usr/local`, symlink,
overlapping, or unmarked nonempty override paths before deletion. It never writes below a system
or Homebrew prefix.
`SCIENCE_DEPS_DEPLOYMENT_TARGET` defaults to `11.0`, matching the app's
`NSMinimumSystemVersion`.

## Exact configure inputs

The pinned source CMake files were inspected before selecting keys. Every requested key must be
present in the resulting `CMakeCache.txt`, and configuration fails if CMake prints
`Manually-specified variables were not used`. The commands below show the exact inputs used by
the builder, with `$ROOT` equal to `build/science-deps`, `$PREFIX` equal to `$ROOT/prefix`, and
`$SDK` equal to `xcrun --show-sdk-path`.

All three commands include:

```text
-DCMAKE_BUILD_TYPE=Release
-DCMAKE_POSITION_INDEPENDENT_CODE=ON
-DCMAKE_C_VISIBILITY_PRESET=hidden
-DCMAKE_CXX_VISIBILITY_PRESET=hidden
-DCMAKE_VISIBILITY_INLINES_HIDDEN=ON
-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
-DCMAKE_INSTALL_LIBDIR=lib
-DCMAKE_INSTALL_PREFIX=$PREFIX
```

These options and their resolved `CMakeCache.txt` values are build-configuration evidence for PIC
and hidden visibility. Verification also checks GDAL's object-library PIC cache key, but it does
not claim an object-by-object relocation or exported-symbol audit of every archive member.

ZSTD:

```bash
cmake -S "$ROOT/src/zstd-1.5.7/build/cmake" -B "$ROOT/build/zstd" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_C_VISIBILITY_PRESET=hidden -DCMAKE_CXX_VISIBILITY_PRESET=hidden \
  -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_PROGRAMS=OFF \
  -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_CONTRIB=OFF
```

PROJ:

```bash
cmake -S "$ROOT/src/proj-9.8.1" -B "$ROOT/build/proj" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_C_VISIBILITY_PRESET=hidden -DCMAKE_CXX_VISIBILITY_PRESET=hidden \
  -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_APPS=OFF -DBUILD_TESTING=OFF \
  -DBUILD_PROJSYNC=OFF -DENABLE_TIFF=OFF -DENABLE_CURL=OFF \
  -DEMBED_PROJ_DATA_PATH=OFF -DEMBED_RESOURCE_FILES=ON \
  -DUSE_ONLY_EMBEDDED_RESOURCE_FILES=ON \
  -DSQLite3_INCLUDE_DIR="$SDK/usr/include" \
  -DSQLite3_LIBRARY="$SDK/usr/lib/libsqlite3.tbd"
```

GDAL:

```bash
cmake -S "$ROOT/src/gdal-3.13.1" -B "$ROOT/build/gdal" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_C_VISIBILITY_PRESET=hidden -DCMAKE_CXX_VISIBILITY_PRESET=hidden \
  -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_PREFIX_PATH="$PREFIX" '-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local' \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_APPS=OFF -DBUILD_PYTHON_BINDINGS=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_SWIG=ON -DBUILD_TESTING=OFF \
  -DENABLE_GNM=OFF \
  -DGDAL_BUILD_OPTIONAL_DRIVERS=OFF -DOGR_BUILD_OPTIONAL_DRIVERS=OFF \
  -DGDAL_ENABLE_DRIVER_GTIFF=ON -DGDAL_ENABLE_DRIVER_VRT=ON \
  -DOGR_ENABLE_DRIVER_GEOJSON=OFF -DOGR_ENABLE_DRIVER_SHAPE=OFF \
  -DGDAL_USE_EXTERNAL_LIBS=OFF -DGDAL_USE_INTERNAL_LIBS=OFF \
  -DGDAL_USE_CURL=ON -DGDAL_USE_SQLITE3=ON -DGDAL_USE_ZSTD=ON \
  -DGDAL_USE_ZLIB=OFF -DGDAL_USE_ZLIB_INTERNAL=ON \
  -DGDAL_USE_JPEG12_INTERNAL=OFF -DENABLE_DEFLATE64=OFF \
  -DGDAL_USE_TIFF=OFF -DGDAL_USE_TIFF_INTERNAL=ON \
  -DGDAL_USE_GEOTIFF=OFF -DGDAL_USE_GEOTIFF_INTERNAL=ON \
  -DGDAL_USE_JSONC=OFF -DGDAL_USE_JSONC_INTERNAL=ON \
  -DGDAL_USE_ARROW=OFF -DGDAL_USE_PARQUET=OFF \
  -DGDAL_FIND_PACKAGE_PROJ_MODE=CONFIG -DPROJ_DIR="$PREFIX/lib/cmake/proj" \
  -DZSTD_DIR="$PREFIX/lib/cmake/zstd" \
  -DCURL_INCLUDE_DIR="$SDK/usr/include" -DCURL_LIBRARY="$SDK/usr/lib/libcurl.tbd" \
  -DSQLite3_INCLUDE_DIR="$SDK/usr/include" \
  -DSQLite3_LIBRARY="$SDK/usr/lib/libsqlite3.tbd" \
  -DEMBED_RESOURCE_FILES=ON -DUSE_ONLY_EMBEDDED_RESOURCE_FILES=ON \
  -DGDAL_OBJECT_LIBRARIES_POSITION_INDEPENDENT_CODE=ON
```

MEM is an always-built GDAL format declared by `gdal_format(mem)`; the builder does not invent a
`-D` key for it, but verifies that the resolved cache contains
`GDAL_ENABLE_DRIVER_MEM:BOOL=ON`. The pinned patch also makes the resolved cache require
`ENABLE_GNM:BOOL=OFF` and `GDAL_USE_SHAPELIB_INTERNAL:BOOL=OFF`.

Before configuring GDAL, the builder independently compiles `gdal_embed_probe.c` as C23. The
GDAL embed options are ON only when that probe succeeds; otherwise both are OFF and verification
requires installed GDAL resource files. On this host both the independent probe and GDAL's
patched upstream compile probe resolved ON, so GDAL and PROJ resources are embedded. The literal
ON values above are this verified host's resolved configure inputs, not an unconditional policy.

## Runtime capability proof

The compiled `science_deps_runtime_probe` links the installed static prefix. It never calls
`GDALAllRegister`; it manually registers only `GDALRegister_GTiff`, `GDALRegister_VRT`, and
`GDALRegister_MEM`, installs the curl handler, and removes every nonlocal VFS except
`/vsicurl/`. Verification then requires exactly those three active drivers, no active COG or GNM,
and exactly `/vsicurl/` among remote VFS prefixes.
GDAL advertises the manually registered MEM driver as both raster- and vector-capable, so the
runtime-derived `active_ogr_drivers` value is `MEM`; it is not evidence that an optional OGR file
format driver was compiled or registered.

The probe creates and reopens a tiled ZSTD GeoTIFF, reads a VRT, reads and writes a MEM dataset,
and warps EPSG:4326 to EPSG:3857 through PROJ. Its `otool -L` output must contain the macOS curl
and SQLite libraries and no dependency outside `/usr/lib` or `/System/Library/Frameworks`.
Installed libraries must be static archives, the probe must link the three manual registration
entry points but not `GDALAllRegister`, the GDAL archive must contain no GNM objects, and no
Homebrew or `/usr/local` library/include/package path may appear in a resolved cache.

Static archive contents are broader than the runtime-active surface. Enabling GTiff necessarily
compiles `cogdriver.cpp.o`, including `GDALRegister_COG` and COG helper code. GDAL curl support also
compiles inactive S3, GS, Azure/ADLS, OSS, and Swift VFS installer symbols. Verification derives
these disclosures from `libgdal.a` members and symbols. The runtime hard gate remains separate:
COG is not registered, cloud handlers are removed, and `/vsicurl/` is the only active remote VFS.

## Verified result

The 2026-07-12 clean build used AppleClang 21.0.0.21000101 on arm64 macOS with SDK paths from
Xcode and four parallel jobs. The final end-to-end guarded clean build completed in 139 seconds.
`du` reported 51 MiB. The prefix contains 404 regular files and 52 symlinks; its only file in
`bin` is `gdal-config`,
and it contains no `.dylib` or `.so` files. The installed third-party libraries are
`libgdal.a`, `libproj.a`, and `libzstd.a`.

`science-deps-manifest.json` is regenerated from runtime-probe output, archive symbol/member
inspection, link inspection, pins, and the three resolved CMake caches on every build or verify.
Runtime claims come from the compiled
probe, while configure facts come from the caches. The manifest excludes itself and ownership
markers from its prefix inventory. After the schema-v3 probe regeneration, its SHA-256 remained
`d594c258e9e22ed7d2ec1eb1d11578d0e715a383eb82101c95ba03ba063d9bc3` across three consecutive
standalone verifies. Its verified capability summary is:

```text
active_raster_drivers: GTiff, MEM, VRT
active_ogr_drivers: MEM
active_remote_vfs: /vsicurl/
enabled: curl, SQLite3, PROJ, ZSTD
runtime inactive: COG, GNM, cloud VFS installers other than /vsicurl/
configured off: Shape/internal Shapelib, apps, Python/SWIG bindings, tests,
                Arrow, Parquet, PROJ remote grids
inactive compiled helpers disclosed: COG registration/helpers and cloud VFS installers
PROJ resources: embedded
GDAL resources: embedded (independent and upstream compiler probes agree)
```

The runtime link dependencies recorded in the manifest are `/usr/lib/libcurl.4.dylib`,
`/usr/lib/libsqlite3.dylib`, `/usr/lib/libc++.1.dylib`, and `/usr/lib/libSystem.B.dylib`; configure
inputs record the active SDK's curl and SQLite `.tbd` files. No dependency library/include/package
cache entry resolves below `/opt/homebrew` or `/usr/local`.

### Concurrent metadata prefetch prototype rebuild

On 2026-07-13, the repository-bound ownership marker, canonical path, child markers, and
non-symlink guards passed for the exact private root `build/science-deps-prefetch`. Only that
owned root was removed. The three already-downloaded source archives were copied from the local
verified `build/science-deps` cache and rechecked against the pins, so this rebuild did not contact
the public network.

The exact clean command used `SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch"`, `--build
--jobs 4`, and completed in 136.87 seconds. A separate `--verify` passed immediately afterward.
The installed prefix is 52,356 KiB (`du -sh`: 51 MiB), with 404 regular files and 52 symlinks.
Its regenerated manifest SHA-256 is
`a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116`.

The resolved parallel HEAD/Range patch hash is
`d545c492eda3a7c9c7faa4ed06334dfd0723c50aa99ca5f62cb6c7ae664255e8`, exactly matching
`GDAL_PREFETCH_PATCH_SHA256`. The rebuilt runtime probe passed its tiled ZSTD GeoTIFF, VRT, MEM,
and PROJ warp operations. It reported raster drivers `GTiff`, `MEM`, and `VRT`; OGR driver `MEM`;
only `/vsicurl/` as an active remote VFS; inactive COG and GNM; and independent/upstream GDAL
resource embedding enabled. These are private-prefix results only; the public latency diagnostic
and any promotion remain pending.
