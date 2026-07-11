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
`packaging/science_deps/checksums.txt`.

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
`SCIENCE_DEPS_BUILD`, and `SCIENCE_DEPS_PREFIX` can relocate those directories. The builder
canonicalizes the install prefix and refuses locations below `/opt/homebrew` or `/usr/local`.
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
  -DGDAL_OBJECT_LIBRARIES_POSITION_INDEPENDENT_CODE=ON
```

MEM is an always-built GDAL format declared by `gdal_format(mem)`; the builder does not invent a
`-D` key for it, but verifies that the resolved cache contains
`GDAL_ENABLE_DRIVER_MEM:BOOL=ON`. PROJ resource embedding resolved ON. GDAL 3.13.1 probed C23
`#embed` as unavailable with AppleClang 21 on this host, so its cache resolved
`EMBED_RESOURCE_FILES=OFF` and the required GDAL data was installed in the prefix instead.

## Verified result

The 2026-07-12 clean build used AppleClang 21.0.0.21000101 on arm64 macOS with SDK paths from
Xcode and four parallel jobs. It completed in 158 seconds. `du` reported 52,604 KiB / 51 MiB.
The prefix contains 406 regular files and 51 symlinks; its only file in `bin` is `gdal-config`,
and it contains no `.dylib` or `.so` files. The installed third-party libraries are
`libgdal.a`, `libproj.a`, and `libzstd.a`.

`science-deps-manifest.json` is regenerated from the three resolved CMake caches on every build
or verify. Its verified capability summary is:

```text
raster drivers: GTIFF, MEM, VRT
OGR drivers: none
virtual file systems: /vsicurl/
enabled: curl, SQLite3, PROJ, ZSTD
disabled: apps, Python/SWIG bindings, tests, Arrow, Parquet, PROJ remote grids
PROJ resources: embedded
GDAL resources: installed data (compiler probe did not support C23 #embed)
```

The system dependencies recorded in the manifest are the active SDK's
`usr/lib/libcurl.tbd` and `usr/lib/libsqlite3.tbd`. No dependency library/include cache entry
resolves below `/opt/homebrew` or `/usr/local`.
