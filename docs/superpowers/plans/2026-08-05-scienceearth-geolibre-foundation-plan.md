# ScienceEarth GeoLibre Foundation Implementation Plan

> Execute in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on
> `codex/scienceearth-agro-climate-v1`. The pre-change boundary is annotated
> tag `ScienceEarth-v0.7.0-geolibre-foundation-baseline` at `42c99caa`.

## Goal

Introduce GeoLibre's strongest workspace-control patterns without replacing
osgSol's OSG renderer, terrain, 3D Tiles, ScienceQuery/Artifact/Provenance
model, isolated science plugin, or EarthContextHub.

## Non-regression boundaries

- Do not change camera, terrain, imagery, 3D Tiles, photo/video generation,
  science provider execution, macOS exit handling, signing, or packaging.
- Do not launch or foreground the desktop application during automated work.
- Keep the ordinary ScienceEarth-off build independent of optional science
  dependencies.
- Project files never persist API keys, bearer tokens, cookies, or secrets.
- Scientific artifacts are immutable references. Project undo/restore may
  change visibility or selection, not delete evidence.

## Slice 1: canonical project and layer contracts

- [x] Add `EarthProject v1`, `EarthLayerDescriptor v2`, camera, temporal,
      capability, artifact-reference, and workspace state value types.
- [x] Add deterministic JSON serialization, strict version checking,
      normalization diagnostics, and a bounded legacy-v0 migration.
- [x] Prove round-trip behavior, duplicate-layer handling, finite/clamped
      numeric state, future-version refusal, and credential omission.
- [x] Add a non-rendering adapter that snapshots registered `OverlayLayer`
      state and restores only safe mutable fields to known runtime layers.
- [x] Build the Earth application and run all non-window offline regressions.

### Slice 1 verification record

- Science enabled: EarthExplorer builds, project tests pass, and the real
  `osgdb_science` runtime test remains registered and passes.
- Science disabled: EarthExplorer and both project tests build without an
  `osgdb_science` target; both project tests pass.
- The existing macOS-only real-plugin CMake test is now gated by
  `OSGSOL_BUILD_SCIENCE`, fixing a pre-existing Science-off configuration
  failure without changing Science-on behavior.
- No application window, listener, package, signing flow, or Desktop copy was
  launched or created during this slice.

## Slice 2: command history

- [x] Add one command envelope for user, AI, and automated workspace changes.
- [x] Add bounded undo/redo with coalescing for continuous opacity/time edits.
- [x] Preserve immutable science artifacts and record artifact associations.
- [x] Publish command history into the bounded AI context.

### Slice 2 verification record

- A renderer-independent command bus now accepts user, AI, and automation
  origins through the same validated envelope.
- Undo/redo stores bounded per-command before/after scalar state rather than
  copying the full project; command metadata is length-bounded. Consecutive
  opacity and time edits coalesce only when origin, command kind, target, and
  gesture key all match.
- Artifact association commands change project references only. Undo never
  calls or owns the science artifact store, and the artifact id remains in the
  retained audit history.
- `commandHistory` is registered in `EarthContextHub` with bounded entries,
  bounded strings, an independent byte ceiling, and explicit applied/undone
  arrays for AI reasoning.
- Science-enabled and Science-disabled EarthExplorer targets both build; no
  application window, listener, package, or Desktop copy was created.

## Slice 3: shared temporal contract

- [x] Add a renderer-independent temporal adapter interface.
- [x] Pilot AlphaEarth, Sentinel-2, and ERA5/ERA5-Land through one controller.
- [x] Keep live data in the same contract with an explicit live clock mode.
- [x] Show requested time, available time, loading, and applied time distinctly.

### Slice 3 verification record

- `EarthTemporalController` owns bounded, renderer-free adapter state and keeps
  availability, requested selection, load state, and applied selection as
  separate values. Every asynchronous transition carries a generation token,
  so an older completion cannot replace a newer request.
- AlphaEarth, Sentinel-2, ERA5-Land, and ERA5 Agricultural Climate adapters are
  created from the runtime provider descriptors. Sentinel-2 keeps its requested
  search interval separate from the actual scene acquisition instant.
- Live sources use the same selection and loading lifecycle with an explicit
  `live-clock` mode; no implicit wall-clock string is treated as fixed data.
- The Science workbench protocol now publishes the temporal state into the
  existing bounded `scienceWorkbench` AI context. Its UI renders four named
  rows: requested time, source availability, loading state, and map-applied
  time. Selecting a new year never relabels the previous result as applied.
- Science-enabled EarthExplorer, the real science plugin, and the headless
  RmlUi presenter build. Science-disabled EarthExplorer also builds without the
  plugin. All 79 non-window offline tests pass, including stale-completion,
  bounded-metadata, protocol, model, and UI runtime coverage.

## Slice 4: processing registry and optional data engines

- [x] Add typed processing capability registration over existing science jobs.
- [x] Persist cost, progress, cancellation, result, warnings, and provenance.
- [x] Add the native DuckDB Spatial/GeoParquet, PMTiles, and Zarr-family host
      boundary only through isolated optional plugins; do not enlarge the
      Science-off runtime or claim absent engines are available.

### Slice 4 verification record

- Every registered `IScienceProvider` now publishes a typed built-in processing
  capability. DuckDB Spatial/GeoParquet, PMTiles v3, and Zarr-family entries are
  explicit unavailable optional-plugin slots, not silently bundled features.
- `ScienceQueryService` owns one immutable current processing snapshot and an
  optional atomic JSON ledger. The product science session enables the ledger
  under the existing research root; queued work, structured progress, exact
  cost bounds, cancellation, terminal artifact id, warnings, and structured
  source provenance survive process boundaries.
- Workbench snapshots and AI science tools expose the same processing record
  and capability catalog. Missing native engines remain visibly unavailable.
- The native extension host validates ABI v1 and bounded metadata, owns module
  lifetime, and supports bounded JSON submit/snapshot/cancel calls. A dynamic
  fake module proves the boundary offline. Concrete DuckDB, PMTiles, and Zarr
  engine binaries remain separately built optional modules and are not shipped
  by this foundation.
- Science-enabled Release builds pass for EarthExplorer, the real Science
  plugin, the processing host, and all new tests. The complete non-window
  offline suite passes 82/82. A separate Science-disabled Release build passes
  EarthExplorer and its complete 33/33 non-window offline suite.
- Real-plugin tests redirect research and processing records to an isolated
  build-tree directory. No production research data, application window,
  listener, package, signing flow, or Desktop copy was touched.

## Verification gates

1. Every production behavior starts with a failing test.
2. Project round-trip and migration are deterministic and credential-free.
3. Unknown or unavailable plugin layers do not break project loading.
4. Existing renderer and science tests remain green.
5. No GUI process, listener, security setting, or extra desktop app is created.
