# ScienceEarth Scientific Data Platform Design

**Date:** 2026-07-11

**Status:** Approved in conversation; awaiting written-spec review

**Baseline:** `v0.2.0` / commit `0e91c7c4b121d80b929d595ea711d3dd0833ee67`

**Boundary tag:** `ScienceEarth`

## 1. Decision

osgSol Earth will evolve from `v0.2.0` into **ScienceEarth**: an AI-native scientific
Earth research platform. AlphaEarth is the first major raster source, not a one-off feature.

The platform will:

- preserve the complete `v0.2.0` behavior as a protected baseline;
- use a trimmed, isolated GDAL stack for scientific raster data;
- keep AI connected to every scientific source through a shared query and evidence model;
- let the Agent plan and execute multi-source research without directly mutating the scene;
- expose research results as cited artifacts that can optionally become map layers or charts;
- add heavy scientific formats in separate provider packs rather than expanding the core bundle
  without bounds.

This design explicitly rejects both of the following:

1. installing and shipping the full Homebrew GDAL dependency tree;
2. replacing the existing basemap, terrain, TIFF, 3D Tiles, camera, or photo paths with GDAL.

## 2. Release and tag policy

`v0.2.0` is the last release before the scientific platform boundary and remains an immutable
rollback point.

- `ScienceEarth` is an annotated, non-moving tag on the exact `v0.2.0` release commit.
- Every later formal release carries two immutable tags:
  - the normal semantic tag, for example `v0.3.0`;
  - the ScienceEarth lineage tag, for example `ScienceEarth-v0.3.0`.
- The plain `ScienceEarth` tag is never moved to a later commit.
- Development branches use the normal `codex/` prefix.
- A release is not a ScienceEarth release unless both tags point to the same verified commit.
- Release notes must state the ScienceEarth phase and the `v0.2.0` regression result.

## 3. Hard constraints

### 3.1 Preserve completed behavior

The following are protected contracts:

- the clean preset removes every selectable overlay, satellite track, selected path, and range;
- the control panel scrolls down and back up;
- satellite trajectories and station range graphics do not survive a clean reset;
- Hong Kong terrain keeps the continuous elevation correction, nested grid, skirts, and camera
  floor behavior;
- Hong Kong 3D Tiles continue refining after altitude changes without holes, distorted ground, or
  height-scale disagreement;
- middle-mouse camera tilt does not shift the selected ground point;
- higher precision loading does not automatically oscillate camera altitude;
- photo generation uses the user-confirmed visible view and never switches to a top view;
- target photography remains a two-turn flow: fly, allow view confirmation, then capture;
- every generated image is independent and must not inherit a previous Hong Kong or other image;
- existing basemap, labels, precipitation, clouds, NDVI, nightlights, GEBCO, feeds, satellites,
  flights, ships, 3D Tiles, and AI tools retain their established behavior when science is off.

### 3.2 AI is part of the platform, not a side feature

Every scientific source must expose at least:

- `describe`: coverage, variables, units, resolution, version, and license;
- `sample`: point or bounded-area numerical access;
- `visualize`: an artifact that can become a map layer;
- `cite`: source, version, query time, and limitations.

Optional capabilities are `timeseries`, `statistics`, `compare`, `change_detection`, and
`similarity`.

There must be no UI-only scientific layer that the Agent cannot inspect and cite.

### 3.3 Research has no implicit scene authority

- Scientific queries do not move the camera.
- Scientific queries do not enable layers merely to obtain data.
- `show_science_artifact` may materialize a result but does not move the camera.
- `fly_to` remains a distinct, explicit scene action.
- A photo captures the visible scene after the existing view-confirmation gate.

## 4. Chosen architecture

The chosen route is a **Scientific Data Fabric plus Agent Research Layer**.

```text
User question
    -> Research Agent
        -> Science Source Registry
        -> Science Query Service
            -> GDAL/STAC raster providers
            -> API/time-series providers
            -> adapters for existing FeedLayer sources
        -> Research Job Manager
        -> Evidence Store
        -> comparison/analysis
        -> cited brief, chart, or optional map artifact

Existing basemap / terrain / 3D Tiles / camera / photo paths bypass this stack.
```

### 4.1 Science Source Registry

Each provider registers a `ScienceSourceDescriptor` with:

- stable source id, display name, category, and provider version;
- spatial and temporal coverage;
- variables, original units, normalized units, and resolution;
- supported query and analysis capabilities;
- license, required attribution, access conditions, and credential requirements;
- expected update cadence, latency, cache lifetime, and current health;
- a provider-owned query adapter.

The Agent discovers sources through this registry instead of carrying every source name and schema
in its prompt.

### 4.2 Science Query Service

The query service is the only public path for manual science UI, Agent tools, and cross-source
analysis. Providers do not call `LayerManager`, the camera, or OSG scene nodes.

The service provides:

- catalog search;
- point sampling;
- bounded raster requests;
- time-series requests;
- regional statistics;
- aligned comparisons;
- artifact materialization;
- cancellation and budget enforcement.

### 4.3 Evidence Store

All results are stored as structured artifacts with provenance. Large raster payloads remain in
the cache; the model receives summaries and stable artifact ids rather than full pixel arrays.

## 5. Unified data contracts

### 5.1 GeoTemporalQuery

Every provider consumes a normalized query containing:

```text
geometry       point | bbox | polygon | current_view
time           instant | interval | explicit year list
variables      provider variables, bands, or derived measures
resolution     requested target resolution and maximum allowed upsampling
aggregation    none | mean | min | max | sum | trend | histogram
output         scalar | table | timeseries | raster | layer artifact
limits         maximum area, bytes, memory, duration, and result cells
purpose        visualize | research | compare | change | export
priority       visible | interactive research | background
generation     cancellation and late-result rejection id
```

Rules:

- WGS84 is the query interchange geometry, but source CRS is preserved in evidence.
- Reprojection, resampling, aggregation, and dequantization are always recorded.
- Low-resolution data is never presented as new high-resolution information.
- NoData uses a mask and is never silently converted to zero.
- Original units are retained alongside normalized units.
- Time distinguishes acquisition time, publication time, forecast initialization, and forecast
  validity.
- A nearest-date substitution outside a source-defined tolerance requires an explicit warning.

### 5.2 ScienceArtifact

```text
artifact_id
query
source_refs[]
numeric_summary
payload_refs[]        raster, table, timeseries, or vector cache references
visualization_hint
evidence[]
warnings[]
processing_version
cache_key
created_at
expires_at
```

### 5.3 EvidenceItem

An evidence item records:

- source id, version, dataset id, and original URL where appropriate;
- requested and actual space/time coverage;
- variables and units;
- fetch or acquisition time;
- processing steps and parameters;
- coverage gaps, uncertainty, and known limitations;
- evidence class: observation, derived calculation, cross-source correlation, or AI inference;
- upstream evidence ids used for a derived result.

AI must not present correlation as causation and must label inference as inference.

## 6. Research jobs

### 6.1 State model

```text
Created -> Planning -> Queued -> Fetching -> Decoding -> Aligning -> Analyzing -> Ready
                                    |           |           |             |
                                    +---------> Partial <----+-------------+

Any active state -> Cancelled
Any processing state -> Failed
```

Requirements:

- every stage is cancellable;
- every provider reports its own progress and error;
- a failed provider can yield `Partial` while independent sources continue;
- partial results list missing sources and their failures;
- a stable query hash deduplicates equivalent work;
- explicit-location research survives camera movement;
- current-view visualization work is cancelled when its generation becomes stale;
- tool execution on the main thread only creates jobs or reads state;
- GDAL, network I/O, decoding, alignment, and analysis never block the main thread.

### 6.2 Scheduler

Priority order is fixed:

1. camera interaction, terrain, and ground selection;
2. 3D Tiles and currently visible basemap content;
3. currently visible scientific layers;
4. user-requested research;
5. background prefetch and catalog refresh.

Initial science limits:

- raster/GDAL: at most 3 concurrent visible RGB jobs;
- API/STAC: at most 4 concurrent network jobs;
- 64D or other heavy analysis: at most 1-2 concurrent jobs;
- global, multi-year, or high-resolution queries must be downsampled or explicitly confirmed;
- the Agent cannot bypass budgets by splitting one disallowed query into many small queries.

## 7. Agent research interface

The platform adds a small set of composable tools rather than one tool per provider:

- `search_science_sources`
- `start_science_research`
- `get_research_job`
- `compare_science_artifacts`
- `run_change_analysis`
- `show_science_artifact`
- `build_research_brief`

Existing tools such as `fly_to`, `set_layer`, `get_view_state`, `show_chart`, feed summaries, and
media generation remain intact.

The existing AI loop already supports multi-round tool use. Science tools must return compact,
structured results and use job polling for long work.

### 7.1 Example research flow

For a question about changes around NVIDIA headquarters from 2018 through 2025, the Agent may:

1. discover AlphaEarth, Sentinel-2, NDVI, VIIRS nightlights, building/road, weather, and event/news
   sources;
2. submit independent bounded queries without changing the view;
3. align spatial resolution and acquisition dates;
4. separate persistent land-surface change from cloud, snow, or seasonal effects;
5. generate a change artifact, a timeline, and an evidence table;
6. explain limitations and label any inferred relationship;
7. show a map layer or chart only when useful to the user's request.

## 8. Scientific provider rollout

### 8.1 Existing-source adapters

Without replacing their runtime code, register adapters for:

- GIBS NDVI and VIIRS imagery;
- GEBCO;
- weather and precipitation;
- earthquakes, disasters, fires, events, satellites, flights, ships, and other existing feeds;
- existing world-query tools where a source can provide structured evidence.

### 8.2 GDAL/STAC raster pack

The first new provider pack contains:

- AlphaEarth annual embeddings;
- Sentinel-2 imagery and selected derived indices through STAC/COG;
- Copernicus DEM;
- generic approved GeoTIFF/COG support.

### 8.3 Atmosphere, ocean, and forecast pack

Use lightweight APIs and hosted tiles first. Add GRIB2, NetCDF, HDF5, and ecCodes as a separate
optional pack only when local numerical-field processing is justified.

### 8.4 Advanced analysis

- local annual embedding change detection follows the raster foundation;
- local similarity is bounded by explicit space/resolution budgets;
- global similarity search requires a precomputed vector index or service and is not solved by
  GDAL alone.

## 9. GDAL build and plugin isolation

### 9.1 Product build

Do not package Homebrew GDAL. Build pinned sources with optional drivers disabled.

Initial pinned inputs:

- GDAL 3.13.1;
- PROJ 9.8.1;
- ZSTD 1.5.7.

Required raster capabilities:

- GTiff for COG reading;
- VRT for controlled orientation and composition;
- MEM for bounded in-memory results;
- warp and coordinate transformation;
- `/vsicurl/` HTTP Range access;
- optional SQLite/GPKG support for the compact catalog.

No command-line tools, Python bindings, SWIG bindings, Arrow, cloud SDKs, database servers, PDF,
NetCDF, HDF, or unrelated drivers ship in the first pack.

### 9.2 Runtime isolation

- GDAL, PROJ, and ZSTD are statically encapsulated in the science plugin where practical.
- Plugin symbols use hidden visibility.
- The main executable does not directly link GDAL.
- The existing OSG TIFF aliases and plugins remain authoritative outside the science plugin.
- Only explicitly allowed GDAL drivers are registered/opened.
- External GDAL plugin discovery is disabled.
- VRT Python and RawRasterBand are disabled.
- Provider VRTs are synthesized in memory from trusted metadata; arbitrary user VRTs are rejected.
- Network source domains are allowlisted.
- If the plugin is absent or fails initialization, the app starts and marks affected sources
  unavailable.

## 10. AlphaEarth data path

The Source Cooperative mirror currently exposes:

- 302,466 COG files and 302,454 VRT files for 2017-2025;
- 8192x8192, 64-band signed-int8 BigTIFFs;
- 1024x1024 band-interleaved tiles;
- ZSTD compression and complete overview pyramids;
- bottom-up source imagery requiring controlled vertical correction;
- a 77.83 MB Parquet index, 539.03 MB GeoPackage index, and 798.15 MB CSV index.

Release-time catalog generation should consume the Parquet index with a development-only tool and
produce a compact read-only SQLite/RTree catalog. The app never downloads the full global index at
runtime.

RGB visualization uses A01/A16/A09 and the documented dequantization:

```text
((value / 127.5) ^ 2) * sign(value)
```

Range access must use independent parallel ranges. A comma-separated multi-range request was
observed to return the complete 270 MB sample object and is forbidden.

## 11. Cache layout

### 11.1 Read-only bundled catalog

The signed app contains read-only source descriptors and compact catalogs. Runtime never writes to
the app bundle.

### 11.2 Regenerable science cache

```text
~/Library/Caches/osgSol Earth/science/
```

Contains rendered tiles, scene lists, small metadata, and regenerable intermediates.

- default total limit: 3 GB;
- configurable range: 1-10 GB;
- LRU eviction;
- separate provider/version/style namespaces;
- cache keys include source version, time, variables, style, processing version, resampling, and
  NoData policy.

### 11.3 Research state

```text
~/Library/Application Support/osgSol Earth/research/
```

Session artifacts expire by default. Only an explicit save operation makes a research package
durable. Clearing the science cache does not delete saved research.

## 12. Failure and shutdown behavior

- plugin failure disables only the affected scientific providers;
- catalog corruption disables only providers that depend on that catalog;
- a Source outage can use rendered cache and does not affect other providers;
- timeout, cancellation, budget rejection, decode error, and NoData return typed job outcomes;
- stale generations can populate cache but cannot be applied to the current layer;
- cleanup cancels and joins science workers before GDAL teardown or plugin unload;
- no science exception may cross into the OSG frame loop.

## 13. Impact boundary

### No direct modification

- EarthManipulator and camera input;
- terrain grid, Hong Kong filter, skirts, and floor state;
- 3D Tiles pager and LOD selection;
- photo view gate, snapshot camera, and image-generation independence;
- satellite trajectory calculations and selection geometry;
- current basemap, label, and Terrarium URL paths.

### Controlled integration points

- `LayerManager`: adds scientific artifact layers and health/progress state;
- `ScienceImagePager`: receives virtual scientific image requests;
- AI tool registry: adds the generic research tool group;
- science UI: source catalog, variables, years, progress, cache usage, and evidence;
- packaging: includes and verifies the optional science plugin and read-only catalog.

## 14. Regression and release gates

### 14.1 Automated baseline

The science-off build must pass the existing overlay, terrain, manipulator, 3D Tiles, AI, media,
and clean-preset tests without changed expectations.

New tests cover:

- plugin-absent startup;
- band selection and dequantization;
- bottom-up correction;
- NVIDIA headquarters and Hong Kong geolocation;
- UTM-zone and antimeridian boundaries;
- NoData transparency;
- Range access with no full-object fallback;
- cancellation and stale-generation rejection;
- bounded cache eviction;
- source failure producing partial research;
- queries and artifact display not changing the camera matrix;
- saved research surviving cache clear;
- source citations and inference labels.

### 14.2 Manual-device baseline

Every formal ScienceEarth candidate verifies:

1. clean preset removes all tracks, ranges, selections, and overlays;
2. panel scroll works in both directions;
3. Hong Kong terrain and 3D Tiles refine without holes, distortion, tofu blocks, or scale drift;
4. middle-mouse tilt keeps the selected location stable;
5. high-precision loading does not oscillate camera altitude;
6. NVIDIA headquarters navigation exposes the target and waits for view confirmation;
7. photo capture preserves the confirmed view;
8. ISS photography uses current position and no prior image input;
9. AlphaEarth direction, year, location, and color are correct;
10. science loading does not starve basemap, terrain, or 3D Tiles;
11. a multi-source research brief contains source/time evidence for each material conclusion.

### 14.3 Quantitative gates

- added app size target: <= 40 MB; stop and review at > 60 MB;
- uncached first scientific tile: median <= 3 s and P95 <= 8 s on the reference network;
- rendered-cache hit: <= 100 ms;
- no complete COG download;
- RGB science active-memory target: <= 300 MB additional;
- science-off behavior matches `v0.2.0`;
- repeated switch/cancel/zoom/quit testing is crash-free;
- only explicit camera tools or user input may change the camera matrix.

## 15. Delivery phases

- **G0:** isolated trimmed-GDAL build, local fixtures, no UI integration.
- **G1:** AlphaEarth RGB behind an experimental flag.
- **G2:** Sentinel-2, Copernicus DEM, and unified Science Query Service.
- **G3:** Agent research jobs, evidence, and multi-source briefs.
- **G4:** bounded local change detection and analysis.
- **G5:** default enablement after all performance and regression gates pass.
- **Later pack:** GRIB2/NetCDF/HDF5 and heavier atmosphere/ocean processing.

Each phase remains reversible and must leave `v0.2.0` behavior intact when its feature flag or
plugin is disabled.

## 16. Implementation planning constraints

The implementation plan must:

- begin from the `ScienceEarth`/`v0.2.0` boundary in an isolated branch/worktree;
- use the trimmed dependency spike as a hard go/no-go gate;
- avoid broad refactors of existing terrain, camera, media, or 3D Tiles code;
- add tests before production behavior changes;
- keep every phase packageable and manually testable;
- defer global similarity and heavy forecast formats until their own approved designs;
- create normal and ScienceEarth release tags only after the formal package and regression gates
  pass.
## 17. Primary references

- GDAL build and driver selection: https://gdal.org/en/stable/development/building_from_source.html
- GDAL remote virtual filesystems: https://gdal.org/en/stable/user/virtual_file_systems.html
- GDAL threading: https://gdal.org/en/stable/user/multithreading.html
- GDAL security: https://gdal.org/en/stable/user/security.html
- AlphaEarth dataset: https://developers.google.com/earth-engine/datasets/catalog/GOOGLE_SATELLITE_EMBEDDING_V1_ANNUAL
- AlphaEarth GCS structure: https://developers.google.com/earth-engine/guides/aef_on_gcs_readme
- Source Cooperative mirror: https://source.coop/tge-labs/aef
