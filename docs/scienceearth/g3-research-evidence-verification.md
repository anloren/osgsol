# ScienceEarth G3 Persistent Research and Evidence Verification

**Date:** 2026-07-19

**Status:** implementation and non-GUI preservation gates PASS; fixed-path Desktop packaging and
human acceptance remain the final delivery boundary.

**Verified source commit:** `5003cddbd6bc43e2e304e30548a75096ff7bddad`

**Protected baseline:** `v0.5.0` / `ScienceEarth-v0.5.0` / `bfb86c1`

## Delivered behavior

G3 extends the existing Agent science tools without replacing the registry, provider, query
service, artifact store, 64-dimensional analysis engine, or explicit layer-display boundary.

- `start_science_research` retains its transient legacy behavior. Supplying
  `research_question` creates a persistent research id; supplying `research_id` attaches another
  source query to an existing record.
- `get_research_job` retains integer `job_id` polling and can also return a durable multi-step
  research record with compact evidence ids.
- `build_research_brief` is the single new tool. It returns structured observations, explicitly
  labelled inferences, limitations, source/time/coverage rows, numbered citations, and Markdown.
- Research manifests and compact evidence survive manager/application restart under
  `~/Library/Application Support/osgSol Earth/research/`. Automated tests inject temporary roots.
- Only compact provenance, numeric summaries, bounded primary metrics, warnings,
  interpretations, and limitations are stored. RGBA pixels, 64D cell arrays, masks, ground grids,
  PCA scores, cluster assignments, and hotspot rasters are not serialized.
- The manager, store, and brief builder have no camera, scene, layer, OSG, GDAL, listener, or local
  server authority. Agent tests require unchanged camera matrices and unchanged layer visibility.

## Scientific interpretation contract

The deterministic brief builder applies source-specific rules before publishing a material
statement:

- AlphaEarth values remain a 64-dimensional latent representation. Distances are mathematical
  relationships in that representation, not named physical variables, domain categories, or
  causal explanations.
- Sentinel-2 natural color describes the selected acquisition and its scene-wide cloud metadata.
  It is not described as a cloud-free composite or independent proof of change.
- Copernicus DEM is a static EGM2008 digital surface model. It is not bare-earth terrain and
  cannot demonstrate temporal change. Display colors are not interpreted when the compact
  evidence lacks numeric elevation summaries.
- Cross-source inference is emitted only when every cited actual coverage shares a non-empty
  common intersection. Time mismatch is reported explicitly.
- Every structured observation, inference, and scientific limitation carries one or more evidence
  ids that resolve to numbered citations. Failed sources appear as missing-source status in a
  partial brief; successful evidence remains usable.

The existing real Copernicus DEM Tokyo verification remains the production-data evidence for G2-3:
a bounded `/vsicurl/` COG window produced numeric DSM summaries and a georeferenced preview without
a full-object fallback. G3 does not introduce or call a different data path.

## Persistence and failure behavior

The Evidence Store uses schema versions `osgsol-science-evidence-v1` and
`osgsol-science-research-v1`, a one-MiB document limit, safe record ids, validated finite numbers,
bounded lists, temporary-file write, `fsync`, close, and atomic rename. It rejects traversal,
symlink escapes, malformed/oversized documents, unknown schemas, and incompatible duplicate ids.

Research state is:

- `running` while any attached step is live;
- `ready` when all attached terminal steps have evidence;
- `partial` when evidence remains alongside a failed or cancelled step;
- `failed` when terminal steps provide no evidence.

Repeated polling of the same ready job reuses its attached evidence id and does not regenerate a
timestamp-dependent duplicate document.

## Automated verification

All commands below ran without launching or foregrounding the Desktop app. The enabled and disabled
CTest commands explicitly excluded the repository's four legacy `network-local` tests, so these
verification runs created no local listener or port.

### Focused and integrated tests

The focused manager, store, type, brief, query-service, and Agent tests passed. The integrated test
created deterministic AlphaEarth, Sentinel-2, and Copernicus DEM artifacts, persisted them under one
research id, destroyed/recreated the manager, and reproduced byte-identical Markdown. A second run
kept AlphaEarth evidence when Sentinel-2 failed and generated an honest partial brief. The persisted
fixture scan found no raw scientific payload.

### Complete ScienceEarth-enabled no-port gate

```sh
cmake --build build/science_g2 -j4
ctest --test-dir build/science_g2 -L offline -LE network-local \
  --output-on-failure
```

Result: **50/50 passed**. This includes research persistence/brief/Agent tests plus the existing
AlphaEarth, 64D math/PCA/clustering/export, Sentinel-2, Copernicus DEM, source registry, query
service, panel/layout, normal-exit guard, terrain, EarthManipulator, 3D Tiles, satellite, AI/chat,
threading, input-safety, and build-contract regressions.

### Freshly reconfigured ScienceEarth-disabled gate

```sh
cmake -S . -B build/science_g2_off \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVERSE_BUILD_EXAMPLES=ON \
  -DOSGSOL_BUILD_SCIENCE=OFF
cmake --build build/science_g2_off -j4
ctest --test-dir build/science_g2_off -L offline -LE network-local \
  --output-on-failure
```

Result: **24/24 passed**. `otool -L` and the generated EarthExplorer link command contain no
`osgSolScience`, AlphaEarth, query-service, research, or evidence target reference.

### Dependency and packaging-policy tests

```sh
SCIENCE_DEPS_ROOT=build/science-deps-g2-v5 \
  packaging/science_deps/build_science_deps.sh --verify
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
```

Results: the private GDAL/PROJ/zstd prefix and manifest passed; the manifest/bundle-audit suites
passed **66/66**.

### Production authority and payload scan

The production research types, store, manager, and brief builder were scanned for camera/scene/OSG
mutation calls, listener/socket APIs, localhost references, raw-payload fields, repository/Desktop
package writes, and unsafe application-bundle paths. No prohibited production match was found.

## Known limitations and final boundary

- Live provider execution remains owned by the existing single-current-job query service. A caller
  should poll a ready/failed source into its research record before replacing it with the next live
  query. Durable evidence and terminal status then survive restart.
- Saved evidence is compact. It can rebuild a cited brief after restart, but it does not promise to
  restore the raster overlay or original 64D arrays after the transient artifact cache is cleared.
- The brief builder is deterministic and deliberately conservative; it does not call an LLM or
  invent physical meaning for latent dimensions.
- The immutable G0 package-size policy debt recorded by earlier verification is unchanged. G3 does
  not hide or relax it.
- This document does not claim that the fixed Desktop application contains commit `5003cdd`.
  Packaging, static bundle/dependency/signature audit, atomic same-name Desktop replacement, and
  one final user-run visual/Quit acceptance are the next gate. Formal tags and synchronization wait
  for that acceptance unless explicitly requested earlier.
