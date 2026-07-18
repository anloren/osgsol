# ScienceEarth G2-2 Sentinel-2 Vertical Slice Design

**Date:** 2026-07-18

**Status:** Approved continuation of the ScienceEarth roadmap

**Release baseline:** `v0.4.0` / `ScienceEarth-v0.4.0` / commit
`76fb4e01be931dbb25fe52abca7511aa1c3c9069`

## 1. Decision

G2-2 adds one real Sentinel-2 Level-2A provider behind the existing
`ScienceQueryService`. The first slice searches Element 84 Earth Search, selects a bounded scene,
reads the scene's 10 m `visual` Cloud-Optimized GeoTIFF through GDAL `/vsicurl/`, and emits the
same georeferenced raster artifact consumed by the current ScienceEarth renderer, UI, and Agent.

It does not create a second science stack, replace the globe basemap, or grant a query camera
authority. It does not introduce a local server, port, security gate, credential, or background
catalog download.

## 2. User outcome

The user can select **Sentinel-2 L2A**, choose a recent time window and maximum acceptable scene
cloud cover, and load a true-color scene over the currently visible location. The result explains:

- what Sentinel-2 L2A and the true-color product are;
- the requested interval and actual acquisition time;
- scene cloud percentage and scene identifier;
- actual footprint, source resolution, and displayed resolution;
- provider, collection, asset URL, processing steps, attribution, and limitations.

Changing source or loading science never moves the camera. A failed, cancelled, stale, or
no-coverage request retains the last good preview and explains why it was retained.

## 3. Scope

### 3.1 Included

- source id `sentinel-2-l2a` and visualization id `natural-color-visual`;
- STAC Item Search 1.0 compatible GET request using collection, bbox, datetime, and bounded limit;
- deterministic local selection: acceptable cloud cover first, then lowest cloud cover, newest
  acquisition, and finally lexical item id;
- strict parsing of item id, bbox, datetime, cloud cover, and the `visual` asset;
- strict allowlist for `earth-search.aws.element84.com` and
  `sentinel-cogs.s3.us-west-2.amazonaws.com/sentinel-s2-l2a-cogs/`;
- one `visual`/`TCI.tif` COG per request, read with `/vsicurl/` and no full-object fallback;
- 256 by 256 RGBA preview and a projected 33 by 33 WGS84 ground grid;
- real `Locating`, `Reading`, `Decoding`, `Validating`, and `Materializing` stages without invented
  percentages;
- source selection, 7/30/90 day window, and 10/20/40/100 percent cloud choices in the UI;
- source-aware Agent research arguments and structured Sentinel-2 evidence;
- offline parser, selection, validation, provider, raster, UI, Agent, AlphaEarth regression, and
  science-off tests;
- one bounded production request before packaging, without a local test server;
- transactional update of `/Users/USER/Desktop/osgSol Earth.app` only after static/package
  gates pass, without launching or foregrounding it.

### 3.2 Explicitly excluded

- Sentinel-2 spectral indices, raw reflectance sampling, mosaicking, or multi-scene composites;
- persistent STAC catalogs, background prefetch, or more than one active science job;
- Copernicus DEM, generic user-supplied COG, or a second remote provider;
- global similarity search or semantic labels for AlphaEarth dimensions;
- changes to terrain, Hong Kong 3D Tiles, basemap, labels, camera input, photo generation,
  satellites, or existing non-science layer runtimes;
- any normal URL GDAL open, `/vsicurl_streaming/`, or HTTP pseudo-driver path for COG assets;
- any test that opens a window, intentionally crashes, aborts, or produces a macOS dialog.

## 4. Official source contract

- STAC Item Search uses `GET https://earth-search.aws.element84.com/v1/search`.
- The collection is `sentinel-2-l2a`.
- Search inputs are `collections`, WGS84 `bbox`, RFC 3339 UTC `datetime`, and `limit=10`.
- The provider does not depend on the optional STAC sort extension.
- The selected asset key is `visual`; it must be a three-band, 10 m, UInt8, COG GeoTIFF.
- A current item uses `properties.datetime`, `properties.eo:cloud_cover`, WGS84 `bbox`, and the
  `assets.visual.href` HTTPS URL.
- Earth Search metadata is discovery evidence; the COG URL is the raster evidence.

References:

- STAC API 1.0 Item Search: <https://api.stacspec.org/v1.0.0/item-search/>
- Earth Search API: <https://earth-search.aws.element84.com/v1>
- Earth Search Sentinel-2 L2A collection:
  <https://earth-search.aws.element84.com/v1/collections/sentinel-2-l2a>
- AWS Open Data Sentinel-2 COG registry:
  <https://registry.opendata.aws/sentinel-2-l2a-cogs/>
- GDAL `/vsicurl/`: <https://gdal.org/en/stable/user/virtual_file_systems.html>

## 5. Query and selection contract

Sentinel-2 preview consumes:

```text
sourceId                 sentinel-2-l2a
geometry                 point plus requestedSpanMeters
time.mode                interval
time.intervalStart       RFC 3339 UTC
time.intervalEnd         RFC 3339 UTC
variables                [visual]
outputKind               raster-layer
visualizationId          natural-color-visual
filters.maxCloudPercent  10, 20, 40, or 100
filters.maxScenes         10
```

The point and span produce a WGS84 search bbox. Antimeridian crossing is rejected in this slice.
The parser rejects malformed JSON, responses above 2 MiB, more than ten retained candidates,
non-finite or out-of-range bbox/cloud values, missing times, non-HTTPS assets, unexpected hosts,
non-`.tif` assets, and non-COG media types.

If no candidate meets the cloud threshold, the request fails as a typed no-matching-scene result;
it does not silently load a cloudier scene. If multiple candidates meet the threshold, ordering is:

1. lower `eo:cloud_cover`;
2. newer `datetime`;
3. lexical item id.

## 6. Network, memory, and latency budgets

- one STAC request, response limit 2 MiB, connect timeout 8 seconds, total timeout 15 seconds;
- at most ten candidate items retained;
- one selected `visual` COG open and one bounded raster window;
- COG access only through `/vsicurl/`, connect timeout 8 seconds, total timeout 25 seconds;
- no full-object fallback and no `/vsicurl_streaming/`;
- requested span 2.56-81.92 km, output 256 by 256 RGBA;
- result RGBA about 256 KiB; ground grid 1089 points; bounded transient GDAL buffers;
- one active provider job; newer work cancels and makes older generations stale;
- STAC or COG failure never removes the last successful artifact.

## 7. Architecture

```text
UI / Agent
    -> GeoTemporalQuery(interval + cloud filter)
    -> ScienceQueryService
    -> ScienceSourceRegistry
    -> Sentinel2Provider
        -> Sentinel2Stac (request, parse, deterministic selection)
        -> Sentinel2Runtime worker
        -> GDAL /vsicurl/ selected visual COG
    -> ScienceArtifact with complete provenance
    -> existing SciencePreviewLayer
```

`Sentinel2Stac` is deterministic and network-free. `Sentinel2Runtime` owns the worker and all
external I/O. `Sentinel2Provider` only adapts the runtime to `IScienceProvider`. Existing
AlphaEarth code remains its own provider and runtime.

## 8. Service evolution

The service's current raster validation is AlphaEarth-specific. G2-2 replaces the hard-coded
signature with descriptor-driven validation:

- geometry and time mode must match source capabilities;
- variables must exactly match a registered visualization;
- raster output remains aggregation `None` and analysis kind `None`;
- interval start/end must both exist and be ordered;
- explicit-year AlphaEarth behavior remains unchanged;
- provider-specific validation is performed before dispatch;
- cost estimates remain bounded and source-aware.

## 9. UI and Agent presentation

The left panel first shows a full-width source selector. AlphaEarth keeps Preview, Point Series,
and Regional Change. Sentinel-2 exposes only True-color Scene. Unsupported modes are not shown or
left disabled without explanation.

Sentinel-2 uses radio/select choices rather than ambiguous sliders:

- time window: 7, 30, or 90 days;
- maximum scene cloud: 10%, 20%, 40%, or Any (100%).

Permanent scientific explanations remain behind `?` help. The main panel shows only actionable
state and short summaries. The result panel shows actual acquisition and evidence fields.

Agent tools accept `source_id`, `time_start`, `time_end`, and `max_cloud_percent`. AlphaEarth's
existing year arguments remain backward-compatible. The Agent may search and display a result but
may not move the camera.

## 10. Acceptance gates

1. parser and selection fixtures are deterministic and reject every malformed/unsafe source;
2. service accepts Sentinel interval raster queries and preserves all AlphaEarth validation tests;
3. local COG fixture proves orientation, georeference, RGB channels, cancellation, and bounded
   output without network;
4. one bounded real Earth Search plus COG request returns a valid cited artifact;
5. UI and Agent expose both sources without unsupported AlphaEarth controls leaking into Sentinel;
6. source switching, failure, cancellation, and stale work retain the correct last good result;
7. complete safe offline and science-off regressions pass;
8. package audits pass without starting the app;
9. the same Desktop app is updated transactionally and handed to the user for manual verification;
10. tagging and synchronization wait for a separate user request after manual acceptance.

