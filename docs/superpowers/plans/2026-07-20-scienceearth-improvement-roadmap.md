# ScienceEarth Post-v0.6.0 Improvement Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the accepted `v0.6.0` ScienceEarth boundary into a terrain-correct, understandable, multi-source scientific research product without regressing the ordinary Earth, camera, 3D Tiles, photo, satellite, input, or normal-Quit behavior.

**Architecture:** Keep the signed Earth application as the host and the ScienceEarth/GDAL implementation behind the existing plugin boundary. Replace the independent fixed-altitude science mesh with a generic host-owned terrain material overlay, then improve research sequencing, analysis parity, UI state, cancellation, release metadata, and verification in bounded release slices.

**Tech Stack:** C++17 ScienceEarth plugin/core, C++14 osgVerse host, OpenSceneGraph/OpenGL, GDAL 3.13.1, ImGui, picojson, CMake/CTest, macOS app packaging.

## Global Constraints

- Baseline commit: `2b4b3bb744cb449ca0a9ec40d18a9a8edd721b50`.
- Baseline tags: `v0.6.0` and `ScienceEarth-v0.6.0`, both immutable and both pointing to that commit.
- Working branch: `codex/scienceearth-g2-3-dem` in the isolated worktree
  `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety`.
- Preserve the user-owned untracked directories
  `packaging/scienceearth/__pycache__/` and `tests/__pycache__/`.
- Never launch or foreground `/Users/USER/Desktop/osgSol Earth.app` during automated work.
- Never open a local listener, change macOS security settings, or use crash suppression as a substitute for a real fix.
- The ordinary Earth build must remain usable with `OSGSOL_BUILD_SCIENCE=OFF`.
- Science code remains behind the plugin ABI. The host may own a generic georeferenced-raster renderer, but it must not include GDAL headers or link ScienceEarth libraries.
- No science operation may move the camera unless the user separately asked the existing world-navigation tool to move it.
- AlphaEarth values remain 64-dimensional latent representations. The product must not invent semantic component names, physical variables, or causal explanations.
- Copernicus DEM remains evidence and a hypsometric visualization of a DSM. It must not silently replace or deform the current Terrarium terrain.

---

## The Terrain-Fusion Decision

The scientific raster will be fused into the **material of the existing elevation-displaced globe terrain**, not placed at an altitude and not used to create a second surface.

Current behavior to remove:

- `applications/earth_explorer/science_preview_layer.cpp` creates a separate ECEF mesh at a fixed
  `PREVIEW_ALTITUDE_METERS = 650.0`.
- It uses `osg::Depth::ALWAYS`, so it can draw through terrain and other surfaces.
- It does not follow later terrain-LOD elevation refinement.

Required behavior:

1. Terrarium continues to generate the actual terrain vertices in
   `readerwriter/TileCallback.cpp`.
2. A host-owned `TerrainScienceOverlay` publishes one georeferenced RGBA texture plus its WGS84
   bounds to the globe `StateSet`.
3. Each TMS terrain tile provides its exact longitude/Web-Mercator-Y bounds to the globe shader.
4. The fragment shader maps the tile UV to WGS84 and samples the scientific raster on the same
   fragment that renders the real terrain triangle.
5. Terrain LOD and elevation updates therefore move the scientific color automatically because
   the color and terrain share the same geometry.
6. No height offset, polygon offset, ray-cast elevation sampling, or duplicate mesh is used.

This gives four non-negotiable invariants:

- **No sinking:** there is no independent scientific surface that can fall below a mountain.
- **No floating:** there is no fixed altitude that can hover above flat ground or water.
- **No z-fighting:** there are not two nearly coincident surfaces.
- **No terrain deformation:** a DEM result is color/evidence unless a future, separately approved
  terrain-replacement feature is designed and explicitly enabled.

The first implementation supports the current north-up, axis-aligned WGS84 artifact contract and
rejects/splits dateline-crossing display artifacts. Source alpha/NoData controls coverage. The
existing ocean compositor remains authoritative; a future bathymetry-under-ocean mode must be a
separate design rather than an accidental side effect.

---

## Release Sequence

### v0.6.1 — Terrain-native display and execution correctness (must do first)

- [ ] Replace the fixed-altitude science preview with the terrain-material path above.
- [ ] Add exact WGS84/Web-Mercator mapping tests, corner-orientation fixtures, NoData handling,
  coastal coverage, and science-off fail-closed tests.
- [ ] Add a plugin ABI v3 host-copy display contract; do not let plugin-owned pixel pointers escape
  a call boundary.
- [ ] Keep a transparent default and preserve identical ordinary-Earth output when no science
  artifact is visible.
- [ ] Add a capability gate for the dedicated texture unit. Unsupported renderers must keep
  analysis working and report that terrain-integrated display is unavailable; they must not fall
  back to the floating mesh.
- [ ] Serialize multi-source provider work so one Agent round cannot cancel its own earlier source
  queries by repeatedly submitting to the single-current `ScienceQueryService`.
- [ ] Expose the already implemented cosine/angular metrics, annual series, PCA, spherical
  clustering, regional quantiles, and hotspots through validated UI and Agent choices without
  sending raw 64D cells to the LLM.
- [ ] Make analysis/display execution visibly progress through locate, validate, fetch, analyze,
  display, and finish/fail states; every click must produce immediate state feedback.
- [ ] Make quit/cancel remain responsive while work is running; provider destruction must still
  cancel and join its workers.
- [ ] Unify release version/phase metadata so package, build contract, docs, and UI cannot disagree.
- [ ] Run the detailed plan in
  `docs/superpowers/plans/2026-07-20-scienceearth-v061-terrain-native-correctness-plan.md`.

### v0.7.0 — Broader research workflows and exports

- [ ] Add bounded multi-area and reference-area comparison recipes after the single-area v0.6.1
  workflow has passed human acceptance.
- [ ] Add reusable saved research recipes and explicit refresh/re-run semantics without silently
  replacing historical evidence.
- [ ] Make cross-source research a first-class ordered recipe: AlphaEarth change, Sentinel-2
  acquisition context, Copernicus DEM terrain context, then a cited deterministic brief.
- [ ] Provide method-specific explanations: what is measured, units/range, what a high value means,
  and what cannot be concluded.
- [ ] Add exportable evidence tables without interpreting pseudo-colors as measurements.
- [ ] Add the ten curated natural-language demonstration prompts to a compact prompt gallery; the
  gallery inserts text but never auto-executes a costly query.

### v0.7.1 — Reliability, persistence, and release gates

- [ ] Move beyond the current bounded in-memory artifact store with an explicit opt-in disk payload
  cache design, checksums, quota, provenance, and deletion UI. Persistent evidence records remain
  separate and authoritative.
- [ ] Split GDAL connect and read budgets, check cancellation between every stage, and add a bounded
  provider-operation watchdog without detaching work that can outlive plugin teardown.
- [ ] Add CI for science-on offline tests, science-off contracts, shader/source checks, package
  scripts, and plugin ABI fixtures.
- [ ] Reconcile old release documentation, generated build phase, package defaults, and the current
  formal version from one canonical release descriptor.
- [ ] Produce a distribution-license inventory for bundled GDAL/curl/sqlite/TIFF/PROJ and data
  attributions before any public distribution claim.
- [ ] Audit the lower-priority engine findings separately: ONNX tensor-offset bounds, Bullet
  constraint lifetime, CPython construction, KTX allocator symmetry, and FileCache concurrency.
  These do not enter the v0.6.1 Earth patch without their own evidence and tests.

---

## Product-Level Acceptance Matrix

| Area | Required evidence | Must remain unchanged |
|---|---|---|
| Terrain-native science display | Corner-colored WGS84 fixture follows the actual elevated TMS mesh at flat, mountain, and oblique views | Terrarium height, HK elevation filter, terrain LOD and camera altitude |
| 3D Tiles | Science raster affects globe terrain shader only | HK 3D Tiles geometry, textures, height scale and paging |
| Science semantics | Numeric evidence and explicit method/limitations | No invented AlphaEarth component labels; DEM not treated as time-varying terrain |
| Multi-source Agent | Ordered steps survive provider success/failure and produce cited partial/ready briefs | Existing single-source tool schemas and camera authority |
| UI/UE | Immediate progress, readable method help, scroll affordance, compact details | Existing normal layer panel, chat, clean preset, input capture |
| Cancellation/Quit | Cancel and Quit remain responsive with a live job; clean user Quit produces no matching new `.ips` | No security-setting changes or crash suppression |
| Package | One fixed `osgSol Earth.app`, canonical version/phase, static audit green | No sibling app, no overwrite during staging, no embedded user API key |

## Manual Geography Set

Automated fixtures are authoritative for numerical mapping. The later human test should cover:

- Hong Kong/Shenzhen coast: north-up orientation, partial coverage, 3D Tiles unaffected.
- Tokyo: mountain/urban transition, multiple science sources, no persistent unsupported-height
  watermark.
- Mount Fuji or another steep mountain: no sinking, floating, or detached border while tilting and
  changing altitude.
- Sydney coast: AlphaEarth footprint and labels remain aligned across land/water edges.
- NVIDIA headquarters: prior visible-camera photo flow remains unchanged.

## Stop Conditions

Do not package a candidate if any of these is true:

- the fixed 650 m mesh or `Depth::ALWAYS` science path still exists;
- science-disabled globe output differs for reasons not covered by an approved shader test;
- plugin failure prevents the ordinary Earth from starting;
- a multi-source request can cancel its own previous step;
- analysis work can make Quit unresponsive;
- the package version, ScienceEarth phase, source commit, and paired tag plan disagree;
- automated work requires opening the Desktop app.
