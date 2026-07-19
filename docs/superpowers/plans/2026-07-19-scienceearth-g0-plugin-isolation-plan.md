# ScienceEarth G0 Plugin Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the remaining ScienceEarth G0 blockers without changing the behavior of the completed AlphaEarth, Sentinel-2, Copernicus DEM, 64-dimensional analysis, AI research, UI, photo, map, or ordinary Earth features.

**Architecture:** Move the complete scientific runtime session behind one versioned, opaque function table exported from `osgdb_science.so`. The Earth executable owns a small `dlopen` bridge but no provider, GDAL, PROJ, ZSTD, scientific panel, or scientific AI implementation. The plugin owns the provider registry, query service, preview node, panels, and AI tool registration for the whole process lifetime. Disable GDAL dynamic-driver autoload because this product explicitly registers its three compiled raster drivers. Keep font discovery deterministic through a bundle-local fontconfig file, then sanitize only the known inactive compiled fallback prefixes during staging packaging. The immutable v0.2 reference and ratchet remain unchanged.

**Tech Stack:** C++17 plugin and science runtime, C++14 Earth host, CMake, macOS `dlopen`/`dlsym`, OpenSceneGraph, ImGui, Python 3 and Bash 3.2-compatible packaging/audit tests, Mach-O `nm`/`otool`/`strings`, CTest.

## Global constraints and preserved behavior

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on `codex/scienceearth-g2-3-dem`.
- Never launch, open, focus, render, overwrite, re-sign, or otherwise touch `/Users/USER/Desktop/osgSol Earth.app`.
- Never create a local listener or run tests labelled `network-local`.
- Do not change the product name, bundle id, signing identity, macOS settings, release tags, immutable v0.2 reference, or G0 ratchet.
- Preserve the three scientific providers, 64-dimensional analysis and evidence semantics, preview mesh placement, layer visibility, left operations panel, right result panel, AI research tools, provenance, error/status presentation, and cancel/lifetime behavior.
- Preserve the ordinary Earth executable when `OSGSOL_BUILD_SCIENCE=OFF`; it must not require or load the scientific plugin.
- Missing or ABI-incompatible plugin must degrade only ScienceEarth: ordinary Earth starts normally and records one explicit warning. The formal Science-on package must require exactly one valid plugin.
- Do not solve G0 by disabling science, deleting scientific data, weakening the audit, or rewriting the baseline.
- Use `PYTHONDONTWRITEBYTECODE=1` for Python tests and never stage existing `__pycache__` directories.

## File map

- `applications/earth_explorer/science_plugin_api.h`: versioned single-anchor host/plugin ABI.
- `applications/earth_explorer/science_plugin_runtime.h/.cpp`: host-only dynamic loader and opaque session facade.
- `applications/earth_explorer/science_plugin_entry.cpp`: plugin-owned providers, service, preview, panel, and AI wiring.
- `applications/earth_explorer/earth_main.cpp`: replace direct science construction with the facade.
- `applications/earth_explorer/EarthControlUI.h`: draw scientific panels through the facade.
- `applications/earth_explorer/CMakeLists.txt`: build/install the module; remove static science from the product executable.
- `science/CMakeLists.txt`: keep testable static internals but install/build one hidden-visibility plugin target.
- `tests/science_plugin_runtime_tests.cpp`: fake-anchor loader/lifetime/forwarding tests without a GUI.
- `tests/science_plugin_contract_tests.py`: actual Mach-O export and main-linkage contract.
- `packaging/science_deps/build_science_deps.sh`: compile GDAL with plugin autoload disabled.
- `tests/science_deps_script_tests.sh`: enforce the GDAL no-autoload configuration and clean archive strings.
- `assets/misc/fontconfig/fonts.conf`: deterministic bundle-local font discovery.
- `packaging/relocate_runtime_prefixes.py`: exact allowlist-only, same-length staging-binary prefix relocation.
- `tests/runtime_prefix_relocation_tests.py`: fail-closed binary relocation tests.
- `packaging/relocate_compiled_paths.py`: same-length staging-only compiled source-root relocation.
- `tests/compiled_path_relocation_tests.py`: fail-closed compiled-path relocation tests.
- `packaging/package_macos.sh`: package plugin, set bundle-local fontconfig resources, relocate known fallbacks before signing, validate no forbidden prefixes.
- `tests/package_macos_tests.sh`: package/resource/relocation contract.
- `docs/scienceearth/g0-plugin-isolation.md`: architecture, behavior preservation, failure mode, evidence, and remaining work.

---

### Task 1: Freeze the plugin ABI and host lifetime contract

**Files:**
- Create: `applications/earth_explorer/science_plugin_api.h`
- Create: `applications/earth_explorer/science_plugin_runtime.h`
- Create: `applications/earth_explorer/science_plugin_runtime.cpp`
- Create: `tests/science_plugin_runtime_tests.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`

**Interfaces:**

```cpp
struct OsgSolSciencePluginApiV1
{
    std::uint32_t abiVersion;
    std::uint32_t structSize;
    void* (*create)(const char* indexPath, char* error, std::size_t errorSize);
    void (*destroy)(void* session);
    osg::Node* (*sceneNode)(void* session);
    void (*setVisible)(void* session, bool visible);
    void (*registerAiTools)(void* session, earthai::ToolRegistry*,
                            LayerManager*, osgVerse::EarthManipulator*);
    void (*drawOperations)(void* session, LayerManager*,
                           osgVerse::EarthManipulator*);
    void (*drawResults)(void* session, LayerManager*);
};

using OsgSolScienceAnchor = const OsgSolSciencePluginApiV1* (*)();
```

The only externally visible symbol is the C anchor `_osgsol_science_g0_probe_anchor`. `SciencePluginRuntime` exposes `load`, `available`, `error`, `sceneNode`, `setVisible`, `registerAiTools`, `drawOperations`, and `drawResults`. Its destructor destroys the opaque session but intentionally keeps the module loaded until process exit so OSG-held node destructors and callbacks cannot jump into an unloaded image.

- [x] **Step 1: Write failing fake-ABI tests**

Cover successful create/forward/destroy, missing file, missing anchor, ABI version mismatch, too-small structure, create failure text, null function pointer rejection, and exactly-once session destruction. The tests must use a tiny test module and never create an OSG viewer or window.

- [x] **Step 2: Verify RED**

Run:

```sh
cmake --build build/science_g3_release --target osgSol_Test_SciencePluginRuntime -j4
ctest --test-dir build/science_g3_release -R SciencePluginRuntime --output-on-failure
```

Expected: configure/build failure because the ABI and loader do not exist.

- [x] **Step 3: Implement the minimum fail-closed loader**

Use `dlopen(path, RTLD_NOW | RTLD_LOCAL)`, `dlsym`, exact ABI/size checks, exact function-pointer checks, and bounded error copying. Do not link the loader to `osgSolScienceCore` or `osgSolSciencePreview`. Keep the module handle alive after session destruction.

- [x] **Step 4: Re-run the focused test**

Expected: PASS with no GUI process and no network activity.

---

### Task 2: Build the single-export plugin without changing science behavior

**Files:**
- Create: `applications/earth_explorer/science_plugin_entry.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`
- Modify: `science/CMakeLists.txt`
- Modify: `tests/science_build_contract_tests.cpp`
- Create: `tests/science_plugin_contract_tests.py`

- [x] **Step 1: Write failing build/export/linkage contracts**

Tests must assert:

- Science-on defines targets `osgSolScienceCore` and `osgdb_science`.
- Science-off defines neither target.
- the installed module is exactly `lib/osgPlugins-3.6.5/osgdb_science.so`;
- `nm -gU` exposes only `_osgsol_science_g0_probe_anchor` from the module;
- the Earth main link command does not contain `osgSolSciencePreview`, `libgdal.a`, `libproj.a`, or `libzstd.a`;
- `nm`/`strings` on the Earth executable contain no G0 GDAL/PROJ/ZSTD science patterns.

- [x] **Step 2: Verify RED**

Run the build-contract CTest and Python contract. Expected: FAIL because the module does not exist and the executable still links the static preview library.

- [x] **Step 3: Implement the plugin session**

The opaque session owns, in safe destruction order:

1. `ScienceSourceRegistry` with AlphaEarth, Sentinel-2, and Copernicus DEM providers;
2. `ScienceQueryService`;
3. `SciencePreviewLayer`;
4. `ScienceEarthPanel`.

Forward existing panel and AI functions without changing their implementations or defaults. Provider registration failures remain warnings and do not remove the other providers. The anchor returns one immutable function table. Build a `MODULE` target named `osgdb_science`, set `PREFIX ""`, `SUFFIX ".so"`, hidden C/C++ visibility, and on macOS pass `-Wl,-exported_symbol,_osgsol_science_g0_probe_anchor`. Install it to `${INSTALL_PLUGINDIR}`.

- [x] **Step 4: Keep existing science unit tests on internal libraries**

Do not move or delete provider/analysis tests. The product executable alone loses the static dependency; focused tests may continue linking the static science libraries for direct behavioral regression coverage.

- [x] **Step 5: Re-run build/export/linkage contracts**

Expected: PASS; one module and one export; no science static stack in main.

---

### Task 3: Rewire Earth, UI, layer, and AI call sites through the facade

**Files:**
- Modify: `applications/earth_explorer/earth_main.cpp`
- Modify: `applications/earth_explorer/EarthControlUI.h`
- Modify: `applications/earth_explorer/CMakeLists.txt`
- Modify: `tests/science_plugin_runtime_tests.cpp`

- [x] **Step 1: Add failing forwarding-order regression tests**

Assert the host calls create before scene-node attachment, uses the same opaque session for visibility, AI registration, operations draw, and results draw, and destroys the session exactly once. Assert an unavailable plugin makes all facade calls safe no-ops.

- [x] **Step 2: Replace direct science types in the host**

Remove provider/service/panel/preview includes and source files from the product executable. Load:

```cpp
BASE_DIR + "/" + OSGPLUGIN_PREFIX + "/osgdb_science.so"
```

with the index at `MISC_DIR + "science/alphaearth/alphaearth.sqlite"`, preserving `OSGSOL_ALPHAEARTH_INDEX`. Attach `sceneNode()` only when available. Capture the runtime facade in the `alphaearth` layer callback, register existing AI tools through it, and inject the facade into `EarthControlUI`.

- [x] **Step 3: Preserve UI layout and failure behavior**

`EarthControlUI` calls the facade at the same two positions where `ScienceEarthPanel` currently draws operations and results. Do not add permanent explanatory text or enlarge either panel. When the plugin is unavailable, omit the science panel/layer and emit one log warning; ordinary Earth controls, Quit, photo, maps, camera, and layers remain unchanged.

- [x] **Step 4: Verify Science-on and Science-off compilation**

```sh
cmake --build build/science_g3_release --target osgVerse_EarthExplorer osgdb_science -j4
cmake --build build/science_g2_off --target osgVerse_EarthExplorer -j4
```

Expected: both PASS. Science-off contains no plugin loader activation and needs no science dependency prefix.

---

### Task 4: Remove GDAL's inactive compiled plugin search path at source

**Files:**
- Modify: `tests/science_deps_script_tests.sh`
- Modify: `packaging/science_deps/build_science_deps.sh`

- [x] **Step 1: Write a failing build-script contract**

Require `-DGDAL_AUTOLOAD_PLUGINS=OFF`, require the generated cache value to be `OFF`, and require the final archive/runtime probe to contain no `/usr/local/lib/gdalplugins`, `/opt/homebrew`, source root, or worktree path.

- [x] **Step 2: Verify RED**

```sh
bash tests/science_deps_script_tests.sh
```

Expected: FAIL because the builder leaves GDAL autoload enabled.

- [x] **Step 3: Disable only dynamic driver autoload**

Add `-DGDAL_AUTOLOAD_PLUGINS=OFF` and cache verification. Do not remove the explicitly compiled GTiff, VRT, MEM, `/vsicurl/`, ZSTD, warp, or embedded PROJ capabilities.

- [x] **Step 4: Rebuild and verify the private dependency prefix**

Rebuild the existing `build/science-deps-g2-v5` prefix with the repository script, then run its compiled runtime probe and manifest verification. Expected active drivers and VFS remain exactly the approved lists while the forbidden GDAL fallback string disappears.

---

### Task 5: Make fontconfig/gettext closure bundle-local and prefix-neutral

**Files:**
- Create: `assets/misc/fontconfig/fonts.conf`
- Create: `packaging/relocate_runtime_prefixes.py`
- Create: `tests/runtime_prefix_relocation_tests.py`
- Modify: `applications/earth_explorer/earth_main.cpp`
- Modify: `packaging/package_macos.sh`
- Modify: `tests/package_macos_tests.sh`

**Relocation allowlist:**

- `/opt/homebrew` -> `@bundleprefix` (same 13 bytes), only in staged `libfontconfig.1.dylib` and `libintl.8.dylib`;
- `/usr/local` -> `@bundleusrxx` (same 10 bytes), only in staged `libfontconfig.1.dylib`.

No other path, library, byte count, or replacement is accepted. The tool must refuse symlinks, non-Mach-O input, missing expected occurrences, unexpected counts, length changes, already signed input, and any remaining `/opt/homebrew` or `/usr/local` string.

- [x] **Step 1: Write failing relocation tests**

Test exact replacement, same-size output, surrounding-byte preservation, allowlist ownership, missing token, unexpected token count, signed-input rejection, symlink rejection, and post-scan rejection.

- [x] **Step 2: Verify RED**

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.runtime_prefix_relocation_tests -v
```

Expected: FAIL because the relocation tool does not exist.

- [x] **Step 3: Add a deterministic bundle fontconfig resource**

The XML must search macOS system, local, and user font directories, use only bundle-neutral/system paths, and avoid a Homebrew cache. After `globalInitialize`, set `FONTCONFIG_FILE` to the bundled file only when the user has not already set it. This makes the patched compiled fontconfig fallback inactive and preserves user override behavior. The app already loads its Chinese UI font by explicit bundled path; do not change that path.

- [x] **Step 4: Relocate only staging copies before signing**

Run the tool after dependency closure/relocation and before code signing. Never edit Homebrew, the OSG runtime SDK, build SDK, baseline app, Desktop app, or any source binary. `libintl` has no application translation catalog in this product; replacing its unused compiled default locale prefix must not alter current UI behavior.

- [x] **Step 5: Extend package tests**

Require the bundled fontconfig file, environment setup source contract, identical dylib byte sizes before/after token replacement, no forbidden path strings in staged Mach-O files, resolved bundle-local dependencies, and successful strict signing verification.

---

### Task 6: Full regression, staging package, and final G0 v2 audit

**Files:**
- Modify: `docs/scienceearth/g0-plugin-isolation.md`
- Modify: `docs/scienceearth/g0-v2-audit-readiness.md`

- [x] **Step 1: Run all Python/Bash offline policy tests**

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests \
  tests.science_bundle_audit_tests \
  tests.science_plugin_contract_tests \
  tests.runtime_prefix_relocation_tests -v
bash tests/science_deps_script_tests.sh
bash tests/scienceearth_release_tests.sh
```

- [x] **Step 2: Build and run the Science-on offline suite**

```sh
cmake --build build/science_g3_release --target install -j4
ctest --test-dir build/science_g3_release \
  -L offline -LE network-local --output-on-failure
```

Expected: all existing 50 offline tests plus new plugin tests PASS. Existing AlphaEarth, Sentinel-2, Copernicus DEM, analysis, panel, AI, exit, camera, photo, map, layer, and layout regressions remain green.

- [x] **Step 3: Build and test Science-off**

Reconfigure the existing `build/science_g2_off` tree if needed, build/install Earth, run its offline tests, and assert no `osgdb_science.so` is installed and no science archive is linked.

- [x] **Step 4: Produce a staging-only formal candidate**

Package to `build/science_g0_plugin_audit/candidate/osgSol Earth.app` with the current built source commit, explicit GLCore OSG runtime, approved AlphaEarth index, and runtime smoke skipped. Do not touch the Desktop app.

- [x] **Step 5: Validate package and G0 v2 audit**

Run package data-manifest validation, `codesign --verify --deep --strict`, `otool` closure validation, the exact one-export contract, and `packaging/audit_macos_bundle.py` against the immutable v0.2 baseline/reference/ratchet.

Required result:

- exactly one `osgdb_science.so`;
- no GDAL/PROJ/ZSTD science stack in main;
- plugin exports only `_osgsol_science_g0_probe_anchor`;
- zero new forbidden paths and zero unresolved/external dependencies;
- science closure `<= 60 MiB` and no size gate in `STOP`;
- Tier A PASS and Tier B PASS;
- overall G0 result PASS, or REVIEW_REQUIRED solely when a non-stop size band is reached.

- [x] **Step 6: Record exact evidence and commit**

Document commit ids, candidate fingerprint, main/plugin/data hashes, four size gates, closure members, export list, finding delta, all test counts, and the explicit statement that no Desktop app was launched or overwritten. Stage only named source/test/doc files; leave both existing `__pycache__` directories untouched.

---

## Plan self-review

- The host/plugin boundary keeps the complete science object graph inside one module, avoiding partial C ABI serialization and preserving current feature behavior.
- The host never unloads plugin code while OSG may retain nodes/callbacks; session shutdown still releases services and worker threads exactly once.
- Science-off remains a compile-time clean path.
- GDAL path removal uses its official no-autoload option and retains the exact explicitly registered capability set.
- fontconfig/gettext changes are staging-only, exact-length, allowlist-only, preceded by a bundle-local runtime configuration, and performed before signing; source/runtime SDKs and the immutable baseline are never mutated.
- The audit policy and immutable baseline are not weakened.
- Every production change has a focused RED test before implementation and a full offline regression gate afterward.
