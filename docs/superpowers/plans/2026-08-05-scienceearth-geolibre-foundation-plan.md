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

- [ ] Add a renderer-independent temporal adapter interface.
- [ ] Pilot AlphaEarth, Sentinel-2, and ERA5/ERA5-Land through one controller.
- [ ] Keep live data in the same contract with an explicit live clock mode.
- [ ] Show requested time, available time, loading, and applied time distinctly.

## Slice 4: processing registry and optional data engines

- [ ] Add typed processing capability registration over existing science jobs.
- [ ] Persist cost, progress, cancellation, result, warnings, and provenance.
- [ ] Add native DuckDB Spatial/GeoParquet, PMTiles, and Zarr-family support only
      through isolated optional plugins; do not enlarge the base runtime.

## Verification gates

1. Every production behavior starts with a failing test.
2. Project round-trip and migration are deterministic and credential-free.
3. Unknown or unavailable plugin layers do not break project loading.
4. Existing renderer and science tests remain green.
5. No GUI process, listener, security setting, or extra desktop app is created.
