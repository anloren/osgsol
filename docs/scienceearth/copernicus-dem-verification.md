# ScienceEarth Copernicus DEM GLO-30 verification

Date: 2026-07-19 (Asia/Shanghai)

## Delivered contract

- Source id: `copernicus-dem-glo-30`
- Public product: Copernicus DEM GLO-30, 2021 public release
- Semantics: digital surface model (DSM), not a bare-earth DTM
- Horizontal reference: WGS 84 (`EPSG:4326`)
- Vertical reference: EGM2008, metres
- Native sampling: 1 arc second, presented as nominal 30 m
- Display: fixed hypsometric colors with transparent NoData
- Access: allowlisted HTTPS COG objects through bounded GDAL `/vsicurl/` windows
- UI time behavior: static product; no year slider and no annual-change claim

The product can include buildings, infrastructure, and vegetation. Its colors are a display
mapping, not natural color or land-cover classes. The source is not used to claim construction
date, named land cover, or change cause.

## Real-source command-line verification

The Desktop application was not launched. The command was:

```sh
./build/science_g2/science/osgSol_Smoke_CopernicusDemProduction \
  35.68 139.76 10000
```

Observed result:

| Field | Value |
|---|---:|
| Source object | `Copernicus_DSM_COG_10_N35_00_E139_00_DEM` |
| Source window | 398 x 326 Float32 cells |
| Display | 256 x 256 RGBA |
| Ground grid | 33 x 33 WGS84 points |
| Source resolution | 30.715 m |
| Display resolution | 39.11363281 m/pixel |
| Center elevation | 14.26977539 m |
| Minimum | 0.0 m |
| Maximum | 75.55609894 m |
| Mean | 17.12419146 m |
| Valid / NoData | 129748 / 0 cells |
| Actual bounds | W 139.70486111, S 35.63458333, E 139.81541667, N 35.72513889 |
| Runtime | 4.30506787 s |
| Access evidence | `GDAL /vsicurl/ bounded window` |
| Full-object fallback | `none` |

Object URL:

`https://copernicus-dem-30m.s3.amazonaws.com/Copernicus_DSM_COG_10_N35_00_E139_00_DEM/Copernicus_DSM_COG_10_N35_00_E139_00_DEM.tif`

The result proves a real public object can be addressed, read as a bounded numeric window,
converted to a georeferenced display artifact, and accompanied by compact scalar and provenance
evidence. It does not prove global coverage; ocean or unavailable geocells remain honest
no-coverage results and do not globally degrade the source.

## Automated preservation checks

The full rebuilt suite completed once with 49/49 passing. During review, the existing `offline`
label was found to include four `network-local` tests. Those tests are not part of the continuing
no-port workflow. Subsequent verification uses:

```sh
ctest --test-dir build/science_g2 -L offline -LE network-local \
  --output-on-failure
```

The protected production areas were not modified: terrain mesh/height logic, EarthManipulator,
3D Tiles, photo generation, satellite rendering, and normal-exit implementation. Contract tests
cover camera authority, source registration, panel scrollbars/wrapping, science-off isolation,
macOS normal-exit guards, terrain, 3D Tiles, satellites, and existing AlphaEarth/Sentinel-2 paths.

## Source authority

- AWS Registry of Open Data, Copernicus DEM: <https://registry.opendata.aws/copernicus-dem/>
- Official bucket layout/readme: <https://copernicus-dem-30m.s3.amazonaws.com/readme.html>
- Copernicus Data Space DEM collection: <https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM>

