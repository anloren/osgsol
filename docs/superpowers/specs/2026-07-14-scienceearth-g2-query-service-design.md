# ScienceEarth G2-1 Unified Query Service Design

**Date:** 2026-07-14

**Release baseline:** `v0.3.0` / `ScienceEarth-v0.3.0` / commit
`929fab38b14fba5b703f050d93eef26e7b744a07`

**Protected rollback boundary:** `v0.2.0` / `ScienceEarth` / commit
`0e91c7c4b121d80b929d595ea711d3dd0833ee67`

## 1. Decision

G2-1 adds a unified `ScienceQueryService`, a source registry, an AlphaEarth provider adapter, and
a visible source catalog. It migrates every existing AlphaEarth consumer behind the service while
preserving the verified `v0.3.0` rendering, camera, year, cancellation, and attribution behavior.

The first G2-1 manual package remains local-index-only. It does not add Sentinel-2, Copernicus DEM,
new credentials, or new external requests. This makes the service boundary human-verifiable before
the second and third G2 provider slices depend on it.

The shipped `v0.3.0` interaction and rendering behavior is the acceptance oracle. If a generic
abstraction conflicts with that behavior, G2-1 adapts the abstraction instead of changing the
verified runtime semantics.

## 2. Scope

### 2.1 Included

- a GDAL-free set of generic science query and artifact contracts;
- a registry that exposes source descriptors and provider health;
- a single-active-job query service with validation, cancellation, and stale-result rejection;
- an AlphaEarth provider adapter around the existing verified `SciencePreviewRuntime`;
- migration of the ScienceEarth UI, AI tools, and preview layer to the service;
- a visible source catalog containing AlphaEarth health, temporal coverage, resolution, variables,
  visualization, and attribution;
- offline contract tests, existing preview regressions, science-off regressions, packaging, and
  manual-device acceptance.

### 2.2 Excluded

- Sentinel-2 and Copernicus DEM provider implementation;
- STAC discovery and new COG URLs;
- simultaneous or persistent multi-job research;
- Evidence Store, multi-source briefs, and G3 orchestration;
- local change detection, vector similarity, GRIB2, NetCDF, or HDF5;
- refactors of terrain, camera, 3D Tiles, photography, or existing non-science layers.

## 3. Approaches considered

### 3.1 Facade-first migration - chosen

The query service owns a generic provider interface while `AlphaEarthProvider` translates between
that interface and the stable `SciencePreviewRuntime`. UI, Agent tools, and the preview layer move
to the service in one bounded slice. The adapter can later be replaced internally without changing
consumers.

This approach has the smallest regression surface and preserves the current GDAL worker,
geolocation, raster-window, and artifact behavior.

### 3.2 Immediate provider rewrite - rejected

Rewriting `SciencePreviewRuntime` directly into a generic provider would remove one adapter layer,
but it would also reopen the row orientation, projected grid, cancellation, and low-terrain
visibility bugs that were closed in `v0.3.0`.

### 3.3 G2 plus G3 job platform - rejected

Adding durable jobs and evidence at the same time would make service correctness impossible to
evaluate independently. Persistent jobs and cross-source evidence remain G3 work.

## 4. Architecture

```text
Science UI ----------+
AI tool registry ----+--> ScienceQueryService --> ScienceSourceRegistry
Preview layer -------+           |
                                  +--> IScienceProvider
                                           |
                                           +--> AlphaEarthProvider
                                                   |
                                                   +--> SciencePreviewRuntime
```

`ScienceQueryService` is the only public data path for the three consumers. It has no OSG, camera,
LayerManager, ImGui, or AI dependency. Consumers may decide whether to display a completed
artifact, but the service and provider have no scene authority.

The service supports one active raster-preview job and one last successful artifact in G2-1. That
is sufficient for the existing UI and AI tools and does not pre-implement G3 scheduling.

Build isolation remains explicit: generic contracts, provider interface, registry, and service
form an `osgSolScienceCore` static target that links no GDAL, OSG, UI, or AI library.
`osgSolSciencePreview` links `osgSolScienceCore` plus the private trimmed GDAL dependencies and
contains `AlphaEarthProvider` and `SciencePreviewRuntime`. A science-off build selects neither
target and retains the protected baseline dependency graph.

## 5. Components and responsibilities

### 5.1 Generic contracts

`science/ScienceQueryTypes.h` defines:

- `ScienceSourceHealth`: `Unavailable`, `Ready`, `Busy`, or `Degraded`;
- `ScienceJobState`: `Unavailable`, `Idle`, `Queued`, `Fetching`, `Ready`, `Failed`, or
  `Cancelled`;
- `ScienceGeometry`: WGS84 point, bounding box, or caller-resolved current view;
- `ScienceTimeSelection`: instant, interval, or explicit year list, with acquisition time kept
  distinct from publication and forecast time;
- `ScienceQueryLimits`: maximum area, bytes, memory, duration, result cells, and upsampling;
- `ScienceVariableDescriptor`: stable id, display name, unit, data kind, and component count;
- `ScienceVisualizationDescriptor`: stable id, display name, visualization kind, channel mapping,
  display range, and legend text;
- `ScienceSourceDescriptor`: id, name, category, provider version, attribution, coverage,
  resolution, variables, visualizations, capabilities, health, and health message;
- `GeoTemporalQuery`: source id, geometry, time selection, variables, target resolution,
  aggregation, output kind, limits, purpose, priority, and visualization id;
- `ScienceSourceReference`: source, provider version, dataset id, original URL, requested and actual
  coverage, variables, units, and processing steps;
- `ScienceRasterPayload`: bounds, source/display resolution, RGBA payload, NoData mask through
  alpha, and exact ground grid;
- `ScienceArtifact`: artifact id, original query, generation, source references, raster payload,
  visualization hint, warnings, processing version, and creation time;
- `ScienceJobSnapshot`: service job id, state, query, progress, message, and last successful
  artifact.

The generic contracts may depend on the existing GDAL-free `ScienceGroundGrid` type. They do not
include GDAL headers or implementation-specific dataset handles.

G2-1 enables a deliberately bounded subset of the generic contract: point or caller-resolved
current-view geometry, one explicit AlphaEarth year, variables `A01`, `A16`, and `A09`, no
aggregation, raster/layer output, and visible or interactive-research priority. Other valid
contract modes return an explicit unsupported-capability result before provider dispatch. This
keeps the public API ready for Sentinel-2 intervals and Copernicus DEM without pretending those
providers already exist.

### 5.2 Provider interface

`science/ScienceProvider.h` defines `IScienceProvider` with these responsibilities:

- return its current `ScienceSourceDescriptor`;
- accept one validated `GeoTemporalQuery` and return a provider generation;
- expose an immutable provider snapshot;
- cancel the current provider generation;
- clear provider-owned transient state.

Provider snapshots carry the provider generation so the service can reject a completion from a
superseded request or previously selected provider.

### 5.3 Source registry

`ScienceSourceRegistry` owns providers by stable source id. Duplicate and empty ids are rejected.
It provides descriptor listing in deterministic id order, exact lookup, and a health refresh that
does not start data access.

The first registry contains exactly one provider, `alphaearth-foundations`. The catalog remains
visible if that provider is unavailable, so the user receives a concrete health message instead of
an empty panel.

### 5.4 Query service

`ScienceQueryService` owns the registry and provides:

```cpp
std::vector<ScienceSourceDescriptor> listSources() const;
std::uint64_t submit(const GeoTemporalQuery& query);
void cancel(std::uint64_t jobId);
void clearArtifact();
ScienceJobSnapshot snapshot();
```

Submission validates the source, advertised capability, geometry, time, variables, visualization,
resolution, output, limits, and the existing 2.56 km through 81.92 km preview-span limits before
calling a provider. Each accepted request receives a monotonically increasing service job id.

The service keeps the last successful artifact while a replacement is loading. A failed or
cancelled replacement changes the job state and message but does not erase the visible artifact.
Only `clearArtifact()` removes it.

`clearArtifact()` does not silently cancel unrelated future work. The current UI Remove action
first cancels the current job id and then clears the artifact, preserving its existing all-gone
behavior.

### 5.5 AlphaEarth provider adapter

`AlphaEarthProvider` owns the current `SciencePreviewRuntime`. It translates the current source
descriptor, preview request, preview state, and artifact into the generic types without changing
the runtime's GDAL worker, compact index, source bands, orientation, projected grid, or cache path.

The adapter records the provider generation returned by `queryPoint()`. It publishes only a
runtime snapshot whose generation matches the active provider generation.

### 5.6 Preview layer

`SciencePreviewLayer` keeps its current name and render implementation but receives a
`ScienceQueryService`. It materializes the service's last successful `ScienceArtifact`. The exact
ground grid, 65 percent valid-pixel opacity, yellow footprint border, back-face culling, and
terrain-independent depth behavior remain unchanged.

### 5.7 Visible source catalog and controls

The existing ScienceEarth panel gains a source-catalog block before query controls. It displays:

- source name and health;
- category and provider version;
- 2017-2025 temporal coverage;
- 10 m native resolution and 64 embedding components;
- false-color visualization and `R=A01`, `G=A16`, `B=A09` mapping;
- attribution and an unavailable/degraded health reason.

AlphaEarth is selected by default. The existing year slider, current-view load, progress, cancel,
hide, show, remove, real coverage, display resolution, dataset id, and attribution behavior remain
available below the catalog.

### 5.8 AI tools

The existing tool names remain stable:

- `search_science_sources` lists registry descriptors and health;
- `start_science_research` accepts optional `source_id` and visualization id in addition to the
  current coordinates and year;
- `get_research_job` returns the generic job and artifact schema;
- `show_science_artifact` changes only layer visibility.

For compatibility, an omitted source id resolves to `alphaearth-foundations`. The tools continue
to use the viewed ground target only when coordinates are omitted. No tool in this group writes a
camera matrix or initiates navigation.

## 6. Data and state flow

1. UI or AI creates a `GeoTemporalQuery` from the viewed target, current year, view span, source,
   variables, visualization, output, limits, purpose, and priority.
2. `ScienceQueryService` validates the request and assigns a service job id.
3. The registry selects the provider, and the service records the returned provider generation.
4. `AlphaEarthProvider` delegates to `SciencePreviewRuntime`.
5. Service snapshots reconcile provider progress only when source id and provider generation still
   match the current job.
6. A matching ready provider artifact replaces the service's last successful artifact atomically.
7. UI, AI, and the preview layer read independent immutable snapshots.

Starting another query cancels the previous provider request before the new provider submission.
Later completion from the cancelled generation is ignored.

## 7. Validation, failure, and cancellation

- Unknown sources, variables, visualizations, and unsupported query modes fail synchronously with
  a precise message.
- Latitude must be finite and inside `[-90, 90]`; longitude must be finite and inside
  `[-180, 180]`.
- Year must be inside the selected source's inclusive range.
- Requested span must remain inside the verified preview bounds.
- An unavailable provider remains in the catalog, rejects submission, and reports why.
- Cancellation is idempotent for the current job and cannot cancel a newer job with an older id.
- Failure and cancellation retain the last successful artifact.
- Clearing an artifact is distinct from cancelling a job.
- Service destruction cancels the provider before provider destruction.
- The UI and AI may toggle the existing science layer only; none of these operations changes the
  camera, terrain, basemap, 3D Tiles, or photo workflow.

## 8. Testing strategy

### 8.1 Offline service tests

A fake provider verifies:

- deterministic catalog listing and duplicate-id rejection;
- query validation before provider dispatch;
- rejection of unsupported geometry, time, aggregation, and output modes before dispatch;
- service/provider generation mapping;
- monotonic service job ids;
- automatic cancellation on replacement;
- stale completion rejection;
- last-success retention across fetching, failure, and cancellation;
- explicit artifact clearing;
- unavailable and degraded health reporting;
- shutdown cancellation.

### 8.2 AlphaEarth adapter and renderer tests

The existing preview regression remains authoritative for row orientation, CRS/affine ground grid,
display resolution, opacity, terrain visibility, and back-face culling. Additional adapter cases
verify complete descriptor, query, progress, error, and artifact translation.

### 8.3 UI and AI contract tests

Tests verify that UI and AI consumers depend on `ScienceQueryService` rather than directly on
`SciencePreviewRuntime`, that source id is accepted by the Agent tool, and that the generic result
contains source, dataset, year, footprint, resolution, visualization, and attribution.

### 8.4 Release regression

- science-enabled service and preview tests pass;
- the complete 20-test science-off suite passes unchanged;
- local-index smoke resolves distinct 2018 and 2025 dataset ids;
- the fixed desktop app passes signature and 1920 by 1080 offscreen rendering checks;
- manual testing covers catalog visibility, year reload, replacement failure retention, cancel,
  hide/show/remove, bidirectional panel scrolling, low-altitude Fuji visibility, and Quit;
- the canonical desktop application is updated in place only after the candidate passes.

## 9. Human-verifiable completion

The G2-1 candidate is complete when the user can open the existing desktop application and:

1. see one catalog source with complete AlphaEarth health and metadata before loading;
2. load the same correctly positioned preview from the selected source;
3. change year and observe automatic reload with a changed dataset id;
4. cancel or encounter a controlled failure without losing the last successful result;
5. ask the Agent to discover and query `alphaearth-foundations` through the generic service;
6. verify that none of those actions moves the camera or changes non-science layers;
7. quit normally.

## 10. Delivery and rollback

G2-1 is developed on `codex/scienceearth-g2-query-service`. It produces an untagged manual-test
desktop candidate first. No release tags are created until the user completes manual acceptance.

Disabling ScienceEarth removes the service, provider, catalog, tools, and artifact layer from the
build. `v0.3.0` / `ScienceEarth-v0.3.0` remains the immediate G1 rollback, while `v0.2.0` /
`ScienceEarth` remains the protected pre-science rollback.
