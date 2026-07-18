# Copernicus DEM GLO-30 Official Source Audit

**Date:** 2026-07-19  
**Scope:** ScienceEarth G2-3 only. This note does not authorize replacing osgSol Earth terrain.

## Selected delivery source

- Dataset: Copernicus DEM GLO-30 Public, 2021 release.
- Delivery: anonymous AWS Open Data bucket `copernicus-dem-30m` in `eu-central-1`.
- Format: Cloud Optimized GeoTIFF, one-degree geocells, DEFLATE floating-point data.
- Object pattern:
  `https://copernicus-dem-30m.s3.amazonaws.com/`
  `Copernicus_DSM_COG_10_<N|S>DD_00_<E|W>DDD_00_DEM/`
  `Copernicus_DSM_COG_10_<N|S>DD_00_<E|W>DDD_00_DEM.tif`.
- A live `HEAD` of the Tokyo `N35/E139` object returned `200`,
  `Accept-Ranges: bytes`, `Content-Type: image/tiff`, and a 36,863,907-byte object.
  Production reads must use bounded GDAL `/vsicurl/` windows and may not fall back to a complete
  object download.

## Scientific meaning

- Copernicus DEM is a **Digital Surface Model (DSM)**. Values represent the top-reflective surface
  and can include buildings, infrastructure, and vegetation. They are not a bare-earth DTM.
- GLO-30 grid spacing is 1.0 arc second. "30 m" is a nominal product name; east-west ground spacing
  varies with latitude.
- Horizontal CRS is WGS 84 / EPSG:4326 for the DGED source.
- Vertical reference is EGM2008 / EPSG:3855 and the vertical unit is metres.
- Copernicus publishes absolute vertical accuracy below 4 m at 90% linear error, relative accuracy
  below 2 m on slopes up to 20%, below 4 m on steeper slopes, and absolute horizontal accuracy below
  6 m at 90% circular error. These are product-level specifications, not per-pixel uncertainty.
- The data were primarily acquired by TanDEM-X during 2011-2015, with older DEMs used for some gap
  filling. The selected AWS representation is the 2021 release. It is therefore a static surface
  model and must not expose a misleading year-change control.
- Ocean-only areas normally have no objects; the AWS registry says a height of zero may be assumed.
  The provider must distinguish an expected ocean absence from a transport or coverage failure.

## Display contract

- The map layer is a hypsometric visualization of numeric DSM height; it is not natural color.
- A stable legend and unit must accompany the colors.
- NoData remains transparent and is never silently treated as zero, except an explicitly identified
  ocean-only missing geocell under the documented AWS convention.
- Evidence must carry actual coverage, source and display resolution, center sample, valid/NoData
  counts, min/max/mean for the bounded window, vertical datum, product release, object URL(s), and
  processing steps.
- The provider may create a science artifact only. It may not call the camera, `LayerManager`,
  terrain, 3D Tiles, photo, satellite, or application-exit paths.

## Official primary references

- AWS Registry of Open Data, Copernicus DEM:
  https://registry.opendata.aws/copernicus-dem/
- AWS Copernicus DEM COG layout and processing:
  https://copernicus-dem-30m.s3.amazonaws.com/readme.html
- Copernicus Data Space Ecosystem, Copernicus DEM collection description and specifications:
  https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM
- Copernicus DEM DOI: https://doi.org/10.5270/ESA-c5d3d65

