# ScienceEarth ERA5 agricultural climate v1

This increment adds two point-series sources to the existing ScienceEarth query, evidence, UI,
and AI paths without changing terrain, imagery, 3D Tiles, camera, or photo behavior.

## Sources

| Source | Period exposed | Source output grid | Annual variables |
| --- | --- | --- | --- |
| ERA5-Land Surface & Soil History | 1950 through the last complete year | Open-Meteo 0.1 degree output grid, about 11 km | 2 m mean temperature, 2 m mean relative humidity, 0-100 cm mean soil moisture |
| ERA5 Agricultural Climate | 1940 through the last complete year | Open-Meteo 0.25 degree ERA5 output grid, about 25-28 km | 2 m mean temperature, total precipitation, FAO-56 reference ET0, shortwave radiation, 2 m mean relative humidity, 10 m mean wind speed |

The public Open-Meteo Historical Weather API is used with an explicit ERA5 or ERA5-Land model.
Requests force GMT, `elevation=nan`, and `cell_selection=nearest`. This disables Open-Meteo's
additional elevation downscaling and land-cell preference. The returned coordinate is retained as
the source-grid center in provenance.

Primary documentation:

- <https://open-meteo.com/en/docs/historical-weather-api>
- <https://open-meteo.com/en/licence>
- <https://cds.climate.copernicus.eu/datasets/reanalysis-era5-single-levels>
- <https://cds.climate.copernicus.eu/datasets/reanalysis-era5-land>

## Scientific contract

- Results are source-grid reanalysis context, not station, sensor, farm, or parcel observations.
- One request covers one WGS84 point and one to nine complete UTC calendar years.
- Every returned day must have a valid, unique ISO calendar date. A year must contain exactly 365
  or 366 dates, and each variable requires at least 95 percent finite daily values.
- Annual means are calculated from valid daily means. Precipitation, ET0, and shortwave radiation
  are annual sums of valid daily totals.
- ET0 is an Open-Meteo FAO-56 calculation derived from ERA5 weather fields; it is not a direct farm
  evapotranspiration measurement and assumes a well-watered reference grass surface.
- No raster is created. The result is shown as labeled annual charts with units, source scale,
  attribution, aggregation method, warnings, and limitations.
- Evidence records retain bounded annual values so the research agent can cite and compare them.

## Runtime boundaries

- Provider construction is offline.
- Only the exact HTTPS Historical Weather endpoint is allowed after a user submits a query.
- Response size is capped at 8 MiB, retry count is zero, and connect/total timeouts are bounded.
- Cancellation, replacement requests, clear, and provider destruction invalidate stale work. A
  blocked-I/O fixture verifies that provider destruction returns promptly.
- An ERA5 failure degrades only that provider; existing AlphaEarth, Sentinel-2, DEM, UI, camera,
  terrain, and AI paths remain independent.

## Verification

The deterministic tests cover source metadata and URL construction, units, leap-year aggregation,
duplicate and incomplete calendars, provider lifecycle/cancellation/destruction, query validation
and cost estimation, UI state and evidence text, AI tool schemas/results, evidence persistence, and
research briefs. The full ScienceEarth offline label and the existing input/layout/thread/exit unit
regressions must pass before a Desktop candidate is updated. The GUI bundle is not launched by
automation; final visual and normal-Quit acceptance belongs to the user.
