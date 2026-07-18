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
