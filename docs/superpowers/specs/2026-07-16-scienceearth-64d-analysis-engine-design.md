# ScienceEarth 64D Analysis Engine and Desktop Reliability Design

**Date:** 2026-07-16

**Status:** Approved in conversation; awaiting written-spec review

**Implementation baseline:** commit `93c4a3923ce6f3d3c334af872528a380d1533abf`

**Current packaged application baseline:** commit
`338bd9d5389d0dac095f33a89202fc0851322d8e`

**Protected rollback boundary:** `v0.2.0` / `ScienceEarth` / commit
`0e91c7c4b121d80b929d595ea711d3dd0833ee67`

## 1. Decision

ScienceEarth will add a scientifically bounded analysis engine for the complete 64-dimensional
AlphaEarth Foundations embedding. It will preserve the fast three-channel false-color preview,
load the complete embedding only when an analysis requires it, and expose point time series,
regional change, local PCA, and deterministic spherical clustering through one shared artifact
model used by the panel and the Agent.

The 64 components are latent representation dimensions. They are not temperature, vegetation,
elevation, land-use probability, or natural-color channels. The product may explain reproducible
geometric relationships in embedding space, but it must not invent a physical meaning for an
individual dimension, principal component, or cluster. A physical attribution is allowed only
when a separate validated labeled model or physically meaningful external dataset supports it,
and that evidence must be cited independently.

This slice also repairs three blockers that make the current desktop build difficult to trust or
test:

- normal application exit must no longer produce a macOS crash report;
- science loading must report real work stages instead of appearing stuck at a fabricated 20%;
- the ScienceEarth panel must remain readable without covering the useful map area or clipping
  explanations.

All three reliability repairs are release gates for the 64D engine, not optional polish.

## 2. User outcomes

After this work, the user can:

1. select a point and inspect its complete annual embedding series from 2017 through 2025;
2. compare two years over a bounded region and see where embedding-space change is concentrated;
3. inspect similarity, distance, coverage, distribution, PCA, and clustering results with clear
   scientific limitations;
4. ask the Agent to run the same research across sources without losing provenance or moving the
   camera;
5. distinguish data acquisition, decoding, analysis, and rendering progress;
6. cancel or retry work without losing the last successful result;
7. collapse results and technical details to restore map space; and
8. quit from the panel, close the window, or use `Cmd+Q` without an abnormal-exit warning.

## 3. Scope

### 3.1 Included

- complete 64-band AlphaEarth Int8 decoding and official dequantization;
- point time series across the complete available annual range;
- bounded two-year regional analysis at a default 128 by 128 analysis grid;
- sequential annual regional summaries without retaining every year's full grid;
- dot product, cosine similarity, cosine distance, Euclidean distance, and angular distance;
- per-cell change rasters, distributions, quantiles, and relative hotspots;
- centered local PCA with explained-variance reporting;
- deterministic spherical k-means with unlabeled cluster identifiers;
- valid-data, norm, coverage, processing, and provenance diagnostics;
- CSV and JSON evidence export;
- compact structured results for Agent research;
- real staged progress, cancellation, stale-result rejection, and last-good retention;
- responsive, progressively disclosed ScienceEarth controls and results;
- root-cause repair and packaged verification of normal macOS exit.

### 3.2 Explicitly excluded

- assigning semantic or physical names to the 64 latent dimensions;
- claiming a PCA axis or cluster represents a physical phenomenon without independent evidence;
- automatic global similarity search over every AlphaEarth tile;
- a precomputed global vector database or server-side approximate-nearest-neighbor index;
- training a supervised land-cover or physical-variable model;
- replacing current terrain, basemap, 3D Tiles, camera, photo, or preview rendering paths;
- silently increasing a bounded request into a global, full-resolution, multi-year analysis;
- introducing a new local server, port, security gate, or unrelated network control.

Global retrieval and validated semantic models may be separate later projects. They are not
prerequisites for scientifically useful local analysis.

## 4. Protected behavior

The following existing behavior is an acceptance oracle and must not regress:

- clean reset removes all selectable overlays, selected satellite tracks, paths, and range rings;
- panel scrolling works in both directions;
- Hong Kong terrain and 3D Tiles continue refining after altitude changes without holes,
  height-scale disagreement, distorted ground, or camera-height oscillation;
- middle-mouse camera tilt does not move the selected ground point;
- photography preserves the user-visible view and never switches to a top view;
- target photography remains a fly, confirm, then capture interaction;
- each generated image remains independent of earlier Hong Kong or other images;
- science queries do not move the camera or alter unrelated layers;
- hiding, showing, removing, cancelling, and replacing a science artifact keep their established
  meanings;
- when science is disabled, existing basemap, labels, weather, feeds, satellites, 3D Tiles, AI,
  and photography retain their prior dependency graph and runtime behavior.

## 5. Approaches considered

### 5.1 On-demand dual path - chosen

The existing false-color preview continues reading only A01, A16, and A09. A separate heavy path
loads all 64 components only for an explicit analysis. Point work retains the complete annual
series; regional work retains two full grids for the selected comparison and computes other
annual summaries sequentially.

This preserves interactive preview speed, bounds memory, and adds complete scientific access
without reopening the verified preview renderer.

### 5.2 Always load all 64 dimensions - rejected

This would make every visualization pay the cost of analysis, increase cancellation latency, and
make ordinary year changes feel slow even when the user only wants a preview.

### 5.3 Precompute a global embedding index first - deferred

A global index would enable broad similarity retrieval, but it requires a separate storage,
versioning, indexing, update, and distribution project. It does not solve the immediate need to
understand a selected point or bounded region and would delay a testable local engine.

## 6. Architecture

```text
ScienceEarth panel --------+
Research Agent ------------+--> ScienceQueryService --> AlphaEarthProvider
Science preview layer -----+           |                       |
                                        |                       +--> Preview runtime
                                        |                            A01/A16/A09 -> RGBA
                                        |
                                        +--> AlphaEarth64DRuntime
                                        |        64-band COG -> embedding artifact
                                        |
                                        +--> ScienceAnalysisEngine
                                                 |
                                                 +--> time series
                                                 +--> similarity/change
                                                 +--> PCA/clustering
                                                 +--> tables/charts/map artifact
```

`ScienceQueryService` remains the only public data path. Preview and analysis are different query
purposes behind the same service, not separate UI-only systems. Providers, runtimes, and the
analysis engine have no camera, `LayerManager`, ImGui, or Agent dependency.

The existing GDAL-free `osgSolScienceCore` target owns contracts, validation, analysis math, job
state, and provenance. The privately linked trimmed-GDAL science target owns COG discovery,
window reads, decoding, and source-specific georeferencing. No GDAL header or handle crosses into
the core target. A science-off build links neither target.

### 6.1 Component boundaries

`ScienceQueryService`:

- validates geometry, years, output, limits, and provider capabilities;
- schedules preview or heavy analysis work;
- owns service job ids, cancellation, stale rejection, and last-good results;
- exposes immutable snapshots to UI, Agent, and renderer.

`AlphaEarth64DRuntime`:

- resolves the correct annual COG and bounded source window;
- reads the 64 Int8 components in cancellable batches;
- applies NoData masking and dequantization;
- produces a georeferenced embedding payload and diagnostics;
- never performs presentation, semantic naming, or scene mutation.

`ScienceAnalysisEngine`:

- consumes only GDAL-free embedding payloads;
- computes deterministic numerical results and evidence metadata;
- generates chart/table/change-raster payloads;
- never fetches data, moves the camera, or invents physical interpretations.

`ScienceEarth UI`:

- creates explicit queries and shows service state;
- presents results with progressive disclosure;
- materializes a map artifact only when the user requests or accepts it;
- does not reimplement analysis math.

`Research Agent`:

- discovers capabilities and starts the same service jobs as the UI;
- receives compact summaries and artifact ids rather than raw arrays;
- can compare evidence across sources while preserving limitations and citations.

## 7. Data contracts

### 7.1 Query extensions

The existing `GeoTemporalQuery` gains supported combinations for:

```text
variables       alphaearth/A01 ... alphaearth/A64 | alphaearth/embedding64
time            one year | explicit year list | inclusive annual interval
output           raster | embedding | timeseries | analysis | export
purpose          visualize | research | compare | change | export
analysis         metrics, PCA options, clustering options, year pair
limits           cells, source bytes, resident memory, duration, resolution
```

Unsupported combinations fail before provider dispatch. A preview request remains byte-for-byte
compatible with the current A01/A16/A09 path.

### 7.2 Embedding payload

A `ScienceEmbeddingPayload` records:

- years and year-to-slice mapping;
- width, height, and exactly 64 float components per valid sample;
- a separate validity mask;
- WGS84 bounds, source CRS, source window, and exact ground grid;
- native and actual analysis resolution;
- requested and actual spatial and temporal coverage;
- source dataset id, provider version, component order, and processing version;
- dequantization, resampling, aggregation, and normalization steps;
- valid count, NoData count, coverage ratio, norm distribution, and warnings.

Large payloads remain in the artifact/cache layer. Immutable snapshots contain shared payload
references; they do not copy multi-megabyte grids on every UI frame.

### 7.3 Analysis payload

A `ScienceAnalysisPayload` may contain:

- point time-series records;
- pairwise metric records;
- regional summary statistics and quantiles;
- a scalar change raster with its mask and ground grid;
- PCA scores, loadings, explained variance, and sample method;
- cluster ids, centroids, concentration, iteration count, and deterministic seed;
- hotspot cells or polygons defined by a stated relative threshold;
- structured interpretation statements and scientific limitation statements;
- references to every source embedding artifact used.

The parent `ScienceArtifact` remains the shared envelope for provenance, warnings, processing
version, visualization hints, and payload references. Existing preview artifacts remain valid.

### 7.4 Evidence and export

CSV export contains coordinates or point id, year, validity, requested metrics, and the 64
components only when the user explicitly selects raw-component export. JSON export retains the
complete query, source references, processing steps, algorithms, parameters, diagnostics,
results, and limitations.

An export never substitutes a color value for an embedding value. The false-color preview is a
visualization, not the analytical dataset.

## 8. AlphaEarth decoding and scientific invariants

### 8.1 NoData and dequantization

Raw value `-128` is NoData and is never treated as zero. Each other signed Int8 component `q` is
converted independently using the documented AlphaEarth transformation:

```text
x = sign(q) * (abs(q) / 127.5)^2
```

The equivalent implementation form may differ, but tests must prove the same result for negative,
zero, and positive values. The artifact records that this transformation was applied.

Quantized integers are never averaged, interpolated, compared, or passed to PCA. All numerical
analysis operates on dequantized floating-point vectors with a validity mask.

### 8.2 Vector validity and norms

- any sample containing NoData in a required component is invalid for complete-64D analysis;
- non-finite or zero-norm vectors are invalid and counted separately;
- raw vector norms are recorded and reported rather than silently forced to one;
- cosine calculations divide by observed norms and do not assume perfect unit length;
- a source-documented unit-vector expectation is checked only when its validation rule is
  available; the processing version records that rule and does not invent an undocumented
  tolerance;
- an unexpected norm distribution produces a warning and is never silently repaired; the query
  fails only when no valid samples remain or the source explicitly defines a violated hard bound.

### 8.3 Spatial aggregation

Spatial aggregation first dequantizes every valid vector. It then computes the component-wise mean
in floating point, records the mean-vector norm as a concentration/heterogeneity diagnostic, and
normalizes the mean direction only for directional comparison or spherical clustering.

Independent scalar resampling of the 64 quantized bands is prohibited. A provider-supplied vector-
correct overview is preferred. Otherwise the runtime reads the necessary source window and performs
the documented vector aggregation locally.

### 8.4 Reproducibility

Every derived artifact records:

- dataset and provider versions;
- query geometry and years;
- actual source windows and valid coverage;
- component order and dequantization version;
- aggregation, sampling, normalization, and metric parameters;
- deterministic seed where an iterative algorithm is used;
- analysis-engine version and creation time.

The same source bytes, query, and processing version must produce the same numerical result.

## 9. Analysis behavior

### 9.1 Point time series

Point mode samples the same WGS84 point for every requested annual dataset, up to the full
2017-2025 range. It returns the complete 64D vector, validity, norm, and coverage/source reference
for each year.

It computes:

- similarity and distance from a selected baseline year;
- similarity and distance between consecutive valid years;
- largest consecutive change and its year interval;
- missing-year gaps without interpolating them;
- trend summaries over the metric series, clearly labeled as embedding-space trends.

### 9.2 Similarity and change metrics

For two valid vectors `a` and `b`, the engine can report:

- dot product `a . b`;
- cosine similarity `(a . b) / (|a| |b|)`;
- cosine distance `1 - cosine_similarity`;
- Euclidean distance `|a - b|`;
- angular distance `acos(clamp(cosine_similarity, -1, 1))`.

Cosine similarity is the primary scale-robust comparison. Other metrics remain visible in
technical details because they reveal different mathematical properties. No threshold is called
physical change; thresholds are named as relative embedding-space thresholds.

### 9.3 Regional pair comparison

Regional mode aligns two selected years to the same exact ground grid, computes metrics only where
both years are valid, and produces:

- a per-cell change raster;
- valid overlap and missing coverage;
- mean, median, standard deviation, minimum, maximum, and selected quantiles;
- relative hotspots using the displayed 90th-percentile threshold by default, with an advanced
  control for changing that relative threshold;
- spatial summaries that retain the actual footprint and resolution.

The default analysis grid is 128 by 128. A 256 by 256 request is an explicit advanced action that
shows its estimated read, memory, and duration cost before submission. Multi-year full regional
grids require the same explicit confirmation. Annual overview charts are otherwise computed
sequentially so only the active comparison grids remain resident.

Similarity and regional change are enabled by default. PCA and clustering are explicit advanced
analyses. PCA returns the first three components by default; spherical clustering defaults to
four clusters and permits two through eight.

### 9.4 Local PCA

PCA is local to the selected artifact and sample set. The engine centers the 64 dequantized
components and does not assign physical names to principal axes. The result records:

- sample selection and valid sample count;
- component centering and any deterministic spatial sampling;
- eigenvalues and explained-variance ratios;
- loadings and projected scores;
- reconstruction or numerical warnings where applicable.

The UI describes a principal component only as a local mathematical direction explaining a stated
share of variance within the selected data. It never labels a component as vegetation, urban
growth, temperature, elevation, or another physical variable.

For reproducible presentation, each principal direction uses a canonical sign: the loading with
the largest absolute magnitude is made positive, with component order breaking ties.

### 9.5 Spherical clustering

Spherical k-means operates on valid normalized directions and uses deterministic initialization,
seed, traversal order, convergence tolerance, and maximum iterations. It reports cluster ids,
counts, centroids, concentration, and convergence diagnostics.

Clusters are displayed as `Cluster 1`, `Cluster 2`, and so on. Colors distinguish clusters but do
not imply land-cover classes. A semantic label can be added only by a later validated model with
independent evidence. After convergence, centroids are sorted lexicographically so equivalent
runs receive the same displayed cluster ids.

### 9.6 Interpretation contract

The product may state:

- similarity decreased or increased between two years;
- embedding-space change accelerated during a stated interval;
- change is spatially concentrated in a stated part of the selected footprint;
- a local PCA direction explains a stated percentage of mathematical variance;
- samples form reproducible unlabeled embedding clusters.

The product may not state that the result proves construction, deforestation, flooding, warming,
crop change, or another physical cause unless a separate physical or labeled source supports the
claim. Cross-source agreement may support an explicitly cited inference, but correlation is not
presented as causation.

## 10. Query sizes, concurrency, and cache

- the preview path remains independent and responsive;
- at most one heavy 64D analysis job is active initially;
- point mode may request all nine annual datasets;
- regional mode defaults to one selected year pair at 128 by 128 cells;
- higher-resolution or retained multi-year grids require explicit cost confirmation;
- source reads occur in cancellable year and component batches;
- projected source bytes, resident memory, result cells, and duration are checked before work;
- the Agent cannot bypass a rejected budget by splitting an equivalent request into many jobs;
- cache keys include source version, geometry, years, grid, components, and processing version;
- a cache hit reuses only an artifact whose complete provenance key matches;
- cancellation or failure never publishes a partial artifact as a successful result.

The service retains the last successful preview and the last successful analysis independently.
Starting a replacement marks the new job active while the last-good result remains visible and
explicitly identified as older. Only a deliberate remove action clears it.

## 11. Job states and honest progress

The analysis state model is:

```text
Idle -> Queued -> Locating -> Reading -> Decoding -> Validating
     -> Aligning -> Analyzing -> Materializing -> Ready

Any active state -> Cancelling -> Cancelled
Any processing state -> Failed
```

Progress is evidence-based:

- locating without a measurable total uses an indeterminate indicator;
- reading shows years, component batches, tiles, or bytes only when the provider knows the total;
- decoding shows completed samples or component batches;
- analysis shows completed algorithm stages and actual counts;
- materialization shows the artifact being created or attached;
- the overall percent is shown only when stage weights and completed units are measurable.

The current hard-coded jump to 5%, then 20%, then 100% is removed. A busy animation is not styled
as an adjustable slider. Stage, message, completed units, total units, elapsed time, and cancellation
state are separate snapshot fields.

If a replacement fails, has no coverage, or is cancelled, the panel keeps the last successful
result and states exactly why the requested replacement was not applied.

## 12. UI and interaction design

The approved layout uses progressive disclosure and protects the map as the primary workspace.

### 12.1 Left operation panel

The default ScienceEarth operation card contains only:

- selected location or current-view footprint;
- year or year pair;
- research type: preview, point series, or regional change;
- one primary `Start analysis` action;
- a collapsed `Advanced settings` section.

Advanced settings contains grid size, metrics, PCA, clustering, export, and explicit cost
confirmation. It is not expanded by default.

### 12.2 Right result panel

The default result card contains only:

- one plain-language embedding-space summary;
- two primary metrics appropriate to the selected analysis;
- one main chart or map legend;
- one visible scientific limitation statement.

PCA, clusters, method, provenance, raw dimensions, and export are collapsed detail sections. The
entire result panel can collapse to a narrow handle so the map regains its width.

### 12.3 Readability rules

- descriptive text wraps within the available content width;
- no scientific sentence is silently clipped by a fixed panel width;
- controls have complete visible labels or a deliberate tooltip/accessibility description;
- the panel has vertical scrolling in both directions;
- results use responsive height and width limits rather than covering half the map;
- resizing the window cannot leave a control outside the scrollable region;
- progress indicators are read-only and visually distinct from sliders;
- year controls use discrete years with visible endpoints and keyboard/button alternatives;
- false-color and cluster legends explain exactly what colors do and do not mean.

### 12.4 State presentation

The UI has explicit presentations for:

- queued or actively reading work with real counters;
- successful analysis;
- no dataset coverage for the selected point/year;
- provider or network failure;
- user cancellation;
- stale request replacement;
- last-good result retained after a failed replacement.

No state replaces a meaningful message with an unexplained red sentence or bare numeric slider.

## 13. Agent integration

The existing tool names remain compatible:

- `search_science_sources` continues to list descriptors and health;
- `start_science_research` continues to start a preview-compatible query and gains explicit
  point-series and regional-analysis options;
- `get_research_job` continues to return current state and artifact references;
- `show_science_artifact` continues to change visibility only.

The shared service additionally exposes the platform-level operations already reserved for
`compare_science_artifacts` and `run_change_analysis` as two separate Agent tools.
`start_science_research` acquires preview, point-series, or regional embedding artifacts;
`compare_science_artifacts` performs bounded metric comparisons between compatible ready artifacts;
and `run_change_analysis` produces change, PCA, or clustering artifacts. All three use the same
query, artifact, budget, cancellation, and provenance contracts.

Tool results contain compact structured summaries, metric tables, limitations, provenance,
artifact ids, and export references. They do not send 64D grids or full raw time-series arrays into
the model context. The Agent may request bounded evidence details by artifact id.

Science tools never write camera matrices, initiate navigation, or silently enable unrelated
layers. `fly_to` remains the only navigation action. Showing an artifact is separate from running
an analysis.

## 14. Failure, cancellation, and stale work

- invalid years, geometry, grid, variables, output modes, and budgets fail synchronously with a
  precise correction message;
- missing coverage is distinct from provider failure;
- every year read, component batch, and long analysis stage observes cancellation;
- an older job id cannot cancel or publish over a newer job;
- a cancelled or failed job retains its diagnostics but cannot become `Ready` later;
- a late provider completion whose generation no longer matches is discarded;
- clearing an artifact is distinct from cancelling work;
- service destruction cancels and joins worker ownership before dependent providers are destroyed;
- UI and Agent read immutable snapshots and never observe a half-written payload.

## 15. Normal-exit crash repair

The panel Quit action currently calls the viewer's normal done path, but the packaged process can
crash during process finalization in the OSG `ApplicationUsage` destruction path. The repair must
identify and remove the ownership, allocator, ABI, heap-corruption, or static-lifetime defect.

The diagnostic sequence is mandatory:

1. reproduce the packaged normal-exit failure in a subprocess and preserve the exit status and
   matching `.ips` report;
2. compare a minimal OSG-linked normal exit with the full Earth application;
3. narrow application components and static finalizers until the failing ownership boundary is
   isolated;
4. use an instrumented or AddressSanitizer build where supported to detect earlier corruption;
5. correct the owner, lifetime, allocator, or build mismatch at its source;
6. retest panel Quit, window close, and `Cmd+Q` in the packaged application.

Forbidden substitutes include `_Exit`, forced process kill, hiding macOS reports, or merely
disabling the crash handler. Acceptance requires exit code zero and no new matching diagnostic
report for all three normal exit paths.

## 16. Testing strategy

### 16.1 Deterministic unit fixtures

Synthetic 64-band Int8 fixtures test:

- `-128` NoData isolation;
- negative, zero, and positive dequantization values;
- component ordering and complete-vector validity;
- norm diagnostics and zero-vector rejection;
- vector-aware aggregation and concentration;
- ground-grid, bounds, mask, and year metadata.

Known floating-point vectors test identical, orthogonal, opposite, scaled, and near-degenerate
cases for dot, cosine, Euclidean, and angular metrics.

### 16.2 Analysis tests

- point time series preserve missing years and baseline selection;
- consecutive change identifies the expected interval;
- regional pair comparison uses only the valid intersection;
- quantiles and relative hotspots are stable and correctly labeled;
- PCA on known low-rank data produces the expected subspace and explained variance;
- PCA output is deterministic under the declared sampling order;
- spherical k-means returns deterministic ids, centroids, and convergence diagnostics;
- no result schema contains invented physical labels;
- CSV and JSON exports reproduce the recorded values and provenance.

### 16.3 Service and runtime tests

- preview and heavy analysis paths remain independent;
- budget rejection occurs before expensive reads;
- cancellation works between years and component batches;
- stale completion cannot replace a newer result;
- failure and cancellation retain the correct last-good artifact;
- immutable snapshots do not copy or expose partial grids;
- cache keys reject mismatched source or processing versions;
- Agent summaries remain bounded and exclude raw 64D grids;
- no science query changes camera or unrelated layer state.

### 16.4 UI tests

- narrow and wide window sizes keep all text accessible;
- the panel scrolls down and back up;
- the result panel collapses and restores map width;
- year controls select discrete supported years;
- progress is read-only and reports actual stages/counters;
- no-coverage, failure, cancellation, success, and last-good states are distinguishable;
- false-color, PCA, and cluster limitations remain visible or directly expandable.

### 16.5 Exit tests

A packaged subprocess test exercises the same viewer-run exit path as the app. Manual acceptance
then covers panel Quit, window close, and `Cmd+Q`. Each must return normally and must not create a
new `osgSol_Earth` diagnostic report.

### 16.6 Protected regression suite

The existing science core, preview, provider, query-service, science-off, and packaging tests must
remain green. Manual regression covers Hong Kong terrain and 3D Tiles, middle-mouse selection,
altitude stability, clean reset, satellite graphics, current-view photography, independent image
generation, and normal non-science layer behavior.

## 17. Acceptance gates

The implementation is ready for a human-verifiable desktop package only when all of these are true:

1. deterministic 64D decoding and analysis tests pass;
2. the complete existing automated science and science-off regression suite passes;
3. the packaged app presents the approved progressive UI without clipped text;
4. preview remains fast and does not load all 64 components unnecessarily;
5. point and regional analysis show actual footprint, resolution, coverage, methods, and limits;
6. Agent and UI consume the same artifacts and neither moves the camera;
7. cancellation, failure, and no-coverage states retain the correct last-good result;
8. Hong Kong, camera, photo, clean reset, and satellite behavior remain unchanged;
9. all three normal exit paths return zero with no new macOS crash report;
10. the fixed app updates `/Users/USER/Desktop/osgSol Earth.app` in place rather than creating
    another differently named desktop package.

Tagging, pushing, and release synchronization occur only after this implementation is built,
automatically verified, manually accepted, and explicitly requested.
