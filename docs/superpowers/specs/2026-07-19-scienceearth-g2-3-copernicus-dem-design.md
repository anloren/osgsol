# ScienceEarth G2-3 Copernicus DEM Design

## Outcome

Add Copernicus DEM GLO-30 as the third real ScienceEarth provider while preserving the v0.5.0
camera, terrain, 3D Tiles, photo, satellite, exit, AlphaEarth, and Sentinel-2 behavior. The provider
returns an independently georeferenced science artifact with numeric DSM evidence and an honest
hypsometric display. It never replaces or modifies the existing Terrarium terrain.

## Source contract

The stable source id is `copernicus-dem-glo-30`. It describes one static 2021 GLO-30 Public DSM:

- variable `surface_elevation`, unit `m`, data kind `digital surface model height`;
- visualization `surface-elevation-hypsometric`, kind `Continuous`;
- point-centered bounded preview with a nominal 30 m target resolution;
- instant/static time semantics, no year selector, no cloud filter, no temporal-change mode;
- EGM2008 vertical datum and WGS84 horizontal coordinates preserved in evidence;
- explicit DSM limitation: buildings, infrastructure, and vegetation can contribute to height.

The first slice supports bounded point-centered map previews because that is the existing common
ScienceEarth visual contract. The artifact also carries numeric center/min/max/mean statistics so
the Agent can use it as scientific evidence without reading colors. It does not claim slope,
bare-earth elevation, flood depth, or temporal change.

## Runtime architecture

`CopernicusDemProvider` implements `IScienceProvider` and delegates asynchronous work to
`CopernicusDemRuntime`. The runtime owns one worker, generation cancellation, late-result rejection,
and injected `ICopernicusDemIo` for deterministic tests.

Production I/O:

1. derives the small set of one-degree geocells intersecting the requested point-centered bounds;
2. constructs only allowlisted AWS GLO-30 URLs;
3. opens COGs through GDAL `/vsicurl/` with the existing bounded HTTP configuration;
4. composes intersecting sources into a trusted in-memory VRT when more than one geocell is needed;
5. reads a bounded 256 by 256 floating-point window plus mask;
6. computes numeric evidence from valid finite cells;
7. converts values through a fixed, documented hypsometric ramp and preserves NoData alpha;
8. builds the same 33 by 33 WGS84 ground grid used by other science overlays.

No complete COG fallback is accepted. The production URL allowlist is exactly the Copernicus DEM
AWS prefix. A missing geocell is classified separately from a transport/decode error. Expected
ocean-only absence may synthesize a zero-height artifact only when every requested cell is absent;
mixed missing land coverage remains a partial-coverage warning or failure, never an invented fill.

## Numeric artifact contract

Extend `ScienceArtifact` with bounded scalar statistics:

```cpp
struct ScienceScalarSummary
{
    std::string variableId;
    std::string displayName;
    std::string unit;
    bool centerValid = false;
    double center = 0.0;
    bool minimumValid = false, maximumValid = false, meanValid = false;
    double minimum = 0.0, maximum = 0.0, mean = 0.0;
    std::uint64_t validCellCount = 0, noDataCellCount = 0;
};
```

The structure contains summaries only, not a second unbounded raster. Byte accounting includes its
owned strings. AlphaEarth and Sentinel-2 remain source-compatible because an empty summary vector
is the default.

## Query and UI contract

`makeCopernicusDemPreviewQuery()` creates `ScienceTimeMode::Instant`, a static product publication
marker, `surface_elevation`, `RasterLayer`, and the hypsometric visualization. The manual panel:

- lists Copernicus DEM alongside AlphaEarth and Sentinel-2;
- shows "static 2021 DSM" instead of a year control;
- places the DSM/DTM and color meaning behind concise help affordances;
- shows fixed legend, metres, EGM2008, actual resolution, numeric summary, and limitations in the
  result card;
- retains bidirectional scrolling and does not enlarge either panel;
- submits without changing camera or layer visibility; explicit Run/Show behavior remains separate.

The Agent's existing `search_science_sources` and `start_science_research` discover and submit the
provider generically. DEM rejects AlphaEarth-only point-series/regional modes with a precise
capability error.

## Protection and failure boundaries

- No changes to `EarthManipulator`, terrain, city/3D Tiles, photo, satellite, or exit code.
- No local HTTP server in production or verification; tests use local files or injected I/O.
- Science-off builds do not compile or register the provider.
- Provider destruction cancels and joins its worker before GDAL teardown.
- Failure changes only this source health; it cannot mark Sentinel-2 or AlphaEarth degraded.
- A failed replacement retains the last successful preview under the existing query-service rule.
- Source health reflects transport/decode failure but does not turn a normal ocean/no-coverage
  outcome into a permanent global degradation.

## Verification gates

- Unit tests: descriptor, query validation, tile URL derivation, longitude/latitude boundaries,
  ocean classification, cancellation, stale generations, evidence, statistics, and color legend.
- Local GDAL fixture: exact geolocation, north-up orientation, mask, center/min/max/mean, multi-tile
  composition, and no full-object behavior.
- Integration: registry, query service retention, panel layout/scroll/help, Agent discovery and
  submission, science-off build contract.
- Real smoke: anonymous Tokyo bounded read, correct EGM2008 evidence, nonzero valid cells, bounded
  source window, no complete object fallback evidence.
- Full no-GUI offline regression before package work.

