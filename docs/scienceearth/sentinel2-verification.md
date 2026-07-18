# Sentinel-2 G2-2 Production Verification

**Status:** PASS

**Verification window ended:** 2026-07-18T14:27:05Z

**Branch:** `codex/scienceearth-g2-query-service`

**Protected baseline:** `v0.4.0` / `ScienceEarth-v0.4.0` / `76fb4e0`

**Execution surface:** command-line smoke only; the Desktop app was not opened or foregrounded.

## Request

The manually invoked `osgSol_Smoke_Sentinel2Production` executable used the complete
`ScienceSourceRegistry -> ScienceQueryService -> Sentinel2Provider -> Sentinel2Runtime` path.
It did not create a window, listener, local server, port, credential, or camera action.

```text
latitude                     35.68
longitude                    139.76
interval                     2026-06-18T00:00:00Z / 2026-07-18T00:00:00Z
maximum scene cloud          20 percent
requested span               2560 m
candidate limit              10
STAC endpoint                https://earth-search.aws.element84.com/v1/search
collection                   sentinel-2-l2a
```

The service estimated a `199692` byte source-read upper bound and a `661528` byte resident-memory
upper bound before dispatch.

## Exact result

```text
state sequence               queued -> locating -> reading -> ready
scene id                     S2C_54SUE_20260710_0_L2A
acquisition                  2026-07-10T01:37:22.464000Z
scene cloud cover            11.17 percent
asset                        https://sentinel-cogs.s3.us-west-2.amazonaws.com/sentinel-s2-l2a-cogs/54/S/UE/2026/7/S2C_54SUE_20260710_0_L2A/TCI.tif
artifact                     256 x 256 RGBA
ground grid                  33 x 33 / 1089 WGS84 points
source resolution            10.0 m
display resolution           10.0 m/pixel
actual WGS84 bounds          139.74568601, 35.66836039, 139.77432677, 35.69172835
recorded elapsed time        4.46615925 s
STAC requests                1
selected COG assets          1
COG access                   GDAL /vsicurl/ bounded window
full-object fallback         none
```

The production path allowlists the Earth Search endpoint and Sentinel COG prefix, opens the asset
only through `/vsicurl/`, disables directory reads, permits only `.tif`/`.tiff`, and has no plain-URL
or `/vsicurl_streaming/` retry branch. “No full-object fallback” here is a verified code-path and
artifact-evidence assertion, not a packet-capture byte claim.

## Repeat disclosure

Two bounded production runs were made. The first passed in `4.52812817 s`, but exposed that the
smoke recorder had not printed the already-present cloud evidence. After adding that output field,
the same request was repeated once; the final complete record above passed in `4.46615925 s` and
selected the same scene. No additional location, interval, or asset was queried.

## Limitations

- `11.17 percent` is scene-wide cloud metadata, not a per-pixel cloud mask.
- `visual`/TCI is a rendered natural-color display product, not raw surface-reflectance values and
  not a cloud-free composite.
- This slice selects one scene and one COG; it does not mosaic adjacent tiles or multiple dates.
- One Tokyo request does not establish a universal latency guarantee.
- This verification proves the provider/runtime/service path, not the final UI or packaged-app
  experience; those remain separate acceptance gates.

The official collection metadata identifies the collection as Sentinel-2 Level-2A, its `visual`
asset as a true-color RGB product, and its temporal extent beginning 2015-06-27:
<https://earth-search.aws.element84.com/v1/collections/sentinel-2-l2a>.

## Safe release regressions

Both release configurations were built completely from committed source without opening or
foregrounding the Desktop app. No selected test created a listener or local server.

### ScienceEarth enabled

The safe offline release set passed **32 of 32** tests in `13.63 s` real time. It covered the
Sentinel-2 STAC parser, bounded GDAL COG runtime, provider, unified query service, AlphaEarth,
source registry, panel, Agent tools, preview/layout contracts, dependencies, and static/fake exit
guards.

### ScienceEarth disabled

A fresh `Release` tree with `OSGSOL_BUILD_SCIENCE=OFF` built successfully. Its safe offline set
passed **14 of 14** tests in `11.34 s` real time. The generated contract contains
`OSGSOL_BUILD_SCIENCE_VALUE 0`; the EarthExplorer compile flags contain
`OSGSOL_BUILD_SCIENCE=0`; and a global-symbol inspection found no `ScienceQueryService`,
`Sentinel2`, or `AlphaEarth` symbol in the resulting executable.

### Deliberate safety exclusions

The regression command excluded four `network-local` tests and did not create any local port. It
also excluded the existing `Ai_Chat`, `Feeds`, `TerrainGrid`, `Tiles3dPaging`, `Satellite`,
`Geospatial`, `EarthManipulator`, `Ais`, and `WorldTools` tests because their legacy assertion
paths call `std::abort()`. `OsgApplicationUsageExit` was excluded because it launches a Viewer
process. Those exclusions avoid generating a macOS crash report or taking focus; they are not
claims that the excluded runtime behaviors were automated in this pass. The new Sentinel-2 tests
use ordinary nonzero exits on failure.

## Manual-candidate correction: false source degradation

The first `0.5.0 manual-test` candidate exposed a real usability defect: a normal no-match result
could leave the source labelled `degraded`, and cloudy regions could appear unable to load.

Root-cause reproduction showed that the client requested an arbitrary first page of ten STAC
items and applied the cloud threshold only after download. It also mapped every failed query,
including “no scene satisfies this threshold”, to whole-source degradation. The corrected request
pushes `eo:cloud_cover <= selected threshold` into Earth Search and asks for deterministic cloud
ascending / acquisition descending sorting before the ten-scene bound. The local threshold check
remains as defense in depth.

The UI now defaults to a 40 percent scene-wide cloud threshold. A no-match result keeps the source
Ready and tells the user to raise the threshold or widen the time window; actual transport, STAC
parsing, and COG failures still degrade the source.

The behavior was checked against live Earth Search data for Hong Kong (`22.3193, 114.1694`). The
30-day catalog contained no scene at or below 20 percent; its least-cloudy scene was approximately
31.16 percent. With the corrected 40 percent default, the complete production path reached Ready
in `3.87905337 s` with:

```text
scene id                     S2B_50QKK_20260712_0_L2A
acquisition                  2026-07-12T03:11:43.272000Z
scene cloud cover            31.16 percent
artifact                     256 x 256 RGBA
source/display resolution    10.0 m / 10.0 m per pixel
actual WGS84 bounds          114.15678411, 22.30756341, 114.18207704, 22.33109638
STAC requests                1
selected COG assets          1
full-object fallback         none
```

After the correction, the complete safe science-enabled offline set passed **32 of 32** tests in
`7.04 s` real time without opening the Desktop app or creating a local listener.

## Manual-candidate correction: invisible north-up preview

The next `0.5.0 manual-test` candidate reached Ready and produced a valid Sentinel-2 raster, but
manual testing showed no visible overlay at any camera height. The defect was in the final render
mesh, not in query coverage, camera height, the COG read, or the layer switch.

AlphaEarth preview fixtures use a south-up ground-grid row order, while Sentinel-2 COGs use the
standard north-up row order. The preview mesh previously emitted one fixed triangle winding and
enabled back-face culling. For a north-up Sentinel grid that winding points every triangle toward
the globe center, so the GPU culls the complete result before fragment shading.

An offline renderer regression first reproduced the defect by requiring a north-up Tokyo
Sentinel quad to have an outward ECEF normal; it failed with the old index order. The mesh builder
now detects the actual grid winding and reverses the indices only when necessary. The same test
also requires the existing south-up AlphaEarth quad to remain outward-facing, preserving the
protected preview behavior while retaining back-face culling so overlays cannot leak through the
far side of the globe.

The focused renderer regression passed after the correction. The complete safe science-enabled
offline set then passed **32 of 32** tests in `26.33 s` without launching the app, creating a
window, or opening a local listener. Visibility in the packaged Desktop app remains a separate
manual acceptance gate.
