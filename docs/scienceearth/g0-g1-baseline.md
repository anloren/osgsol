# ScienceEarth G0/G1 Baseline Contract

## Immutable release boundary

The protected baseline is osgSol Earth `v0.2.0`, commit
`0e91c7c4b121d80b929d595ea711d3dd0833ee67`. The annotated `ScienceEarth` boundary tag points to
that exact commit and is immutable. The plain `ScienceEarth` tag is never moved to a later commit.

Only a user-approved formal release receives the immutable tag pair `vX.Y.Z` and
`ScienceEarth-vX.Y.Z`, and both tags must dereference to the same verified commit. Development and
intermediate acceptance commits receive neither release tag.

## Protected behavior

The approved ScienceEarth design protects all of the following contracts:

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

## G0 boundary and limits

G0 covers only a trimmed GDAL/PROJ/ZSTD dependency build, isolation proof, bounded COG byte-range
proof, and bundle-cost proof. It uses local fixtures and has no UI integration. It must not replace
the existing basemap, terrain, TIFF, 3D Tiles, camera, or photo paths with GDAL, and it must not
install or ship the full Homebrew GDAL dependency tree.

G0 is a hard go/no-go gate. Tasks 7-16 of the approved implementation plan must not begin unless
Task 6 records `G0_DECISION=GO`. A failed gate requires a product decision; it does not permit
relaxing these limits.

| Gate | Pass condition |
|---|---|
| Existing behavior | Science-off targeted regression suite passes unchanged |
| Link isolation | Main executable and non-science libraries have no GDAL/PROJ/ZSTD references |
| System isolation | Bundle has no `/opt/homebrew`, `/usr/local`, build-tree, or source-tree runtime references |
| Added bundle size | Target `<= 40 MiB`; `40-60 MiB` requires explicit review; `> 60 MiB` is STOP |
| AlphaEarth correctness | NVIDIA HQ and Hong Kong fixture queries select expected tile/year, orientation, RGB bands, and dequantization |
| HTTP behavior | Requests are bounded byte ranges; no response equals the complete 270 MB source COG |
| Network latency | Uncached first RGB: median `<= 3 s`, P95 `<= 8 s`; rendered-cache hit `<= 100 ms` on the reference setup |
| Active memory | AlphaEarth RGB activity adds `<= 300 MB` on the reference test |
| Camera authority | Search/start/poll/show do not invoke `fly_to` or mutate the manipulator |
| Cache | Separate bounded science cache with deterministic eviction and no use of the existing terrain cache |
| Missing plugin/offline | App starts normally; science reports unavailable/partial without affecting existing layers |
