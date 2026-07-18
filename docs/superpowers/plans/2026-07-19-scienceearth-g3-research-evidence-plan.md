# ScienceEarth G3 Persistent Research and Evidence Implementation Plan

> Execute after the G2-3 completion gate in the same isolated worktree. Do not launch the Desktop
> app, create a local server, or request intermediate manual confirmation.

**Goal:** Deliver restart-safe Agent research jobs, compact provenance evidence, and cited
AlphaEarth/Sentinel-2/Copernicus DEM briefs while preserving every existing tool and scene boundary.

**Architecture:** Keep live execution in `ScienceQueryService`; add a GDAL-free persistent evidence
store, research manager, and deterministic brief builder in `osgSolScienceCore`; connect them only at
the Agent tool boundary.

**Tech stack:** C++17, picojson, filesystem/atomic rename, existing Agent `ToolRegistry`, CMake/CTest.

## Task 1: Define bounded research and evidence contracts

**Files:**

- Create `science/ScienceResearchTypes.h`
- Create `science/ScienceResearchTypes.cpp`
- Create `science/ScienceResearchTypesTest.cpp`
- Modify `science/CMakeLists.txt`

1. Write failing tests for stable state names, valid transitions, size/list limits, evidence ids,
   source/time/coverage fields, scalar summaries, primary metrics, and citation references.
2. Require finite numeric values, bounded strings/lists, unique evidence ids, and explicit schema
   version `osgsol-science-evidence-v1`.
3. Implement immutable value contracts and validation helpers with no GDAL/OSG dependency.
4. Run GREEN and commit: `feat(scienceearth): define persistent research evidence`.

## Task 2: Implement the atomic Evidence Store

**Files:**

- Create `science/ScienceEvidenceStore.h`
- Create `science/ScienceEvidenceStore.cpp`
- Create `science/ScienceEvidenceStoreTest.cpp`
- Modify `science/CMakeLists.txt`

1. Write failing temporary-directory tests for artifact conversion, JSON round-trip, restart load,
   atomic replacement, malformed JSON, unknown schema, oversized input, non-finite values, path
   traversal, symlink escape, duplicate incompatible evidence, and missing records.
2. Convert `ScienceArtifact` to compact evidence without copying `rgba`, embedding values/masks,
   scalar rasters, PCA scores, or cluster assignments.
3. Persist `research.json` and bounded evidence files with temporary write, flush, close, and atomic
   rename. Validate the root and every read before publication.
4. Ensure a cache clear cannot delete the independent injected research root.
5. Run store/type/artifact tests GREEN. Commit:
   `feat(scienceearth): persist compact evidence records`.

## Task 3: Implement persistent research-job management

**Files:**

- Create `science/ScienceResearchManager.h`
- Create `science/ScienceResearchManager.cpp`
- Create `science/ScienceResearchManagerTest.cpp`
- Modify `science/CMakeLists.txt`

1. Write failing tests for stable id creation, attach-to-existing, live step polling, multiple source
   steps, ready/partial/failed transitions, cancellation, duplicate poll idempotence, restart, and
   maximum record/step limits.
2. Implement manager methods to create/load a record, add a live query step, observe snapshots,
   persist ready artifact evidence, and return compact durable status.
3. Keep all service/provider pointers outside the manager; it consumes snapshots/artifacts only.
4. Run GREEN and commit: `feat(scienceearth): manage persistent research jobs`.

## Task 4: Build deterministic cited multi-source briefs

**Files:**

- Create `science/ScienceResearchBrief.h`
- Create `science/ScienceResearchBrief.cpp`
- Create `science/ScienceResearchBriefTest.cpp`
- Modify `science/CMakeLists.txt`

1. Write failing three-source tests requiring a scope/status section, source/time/coverage table,
   source-bound observations, labelled inference, limitations, numbered citations, and Markdown.
2. Add negative tests: no spatial overlap, temporal mismatch, AlphaEarth semantic overclaim,
   Sentinel cloud-free overclaim, DEM bare-earth/time-change overclaim, missing citation, and one
   failed provider producing a partial brief.
3. Implement deterministic evidence selection and citation coverage. Every observation/inference
   stores the evidence ids it uses; Markdown renders matching citation numbers.
4. Never infer named land cover or cause from AlphaEarth latent dimensions. Never interpret DEM
   color pixels when numeric summaries are absent.
5. Run GREEN and commit: `feat(scienceearth): build cited multi-source briefs`.

## Task 5: Extend Agent tools compatibly

**Files:**

- Modify `applications/earth_explorer/science_ai_tools.h`
- Modify `applications/earth_explorer/science_ai_tools.cpp`
- Modify `applications/earth_explorer/science_ai_tools_test.cpp`
- Modify `applications/earth_explorer/earth_main.cpp`
- Modify `applications/earth_explorer/CMakeLists.txt`

1. Write failing tests preserving all existing tool names/legacy schemas and adding exactly one new
   tool, `build_research_brief`.
2. Test transient legacy start, persistent start with `research_question`, attach with `research_id`,
   live poll update, durable `get_research_job`, restart reconstruction, three-source brief, partial
   failure, and absent/corrupt research root.
3. Record manipulator matrix and layer visibility before/after every new research operation. Require
   no change. Keep explicit `show_science_artifact` behavior unchanged.
4. Derive the default root as `~/Library/Application Support/osgSol Earth/research`; allow a test-only
   injected path through the registration interface rather than a global production setting.
5. Return compact JSON only; assert absence of raw RGBA, embeddings, masks, PCA scores, cluster
   assignments, and full raster grids.
6. Run AI, query-service, provider, and camera-authority tests GREEN. Commit:
   `feat(scienceearth): connect persistent Agent research`.

## Task 6: Integrated restart, partial-source, and preservation gates

**Files:**

- Create `science/ScienceResearchIntegrationTest.cpp`
- Modify `science/CMakeLists.txt`
- Create `docs/scienceearth/g3-research-evidence-verification.md`
- Modify only if required by evidence: package audit manifests/tests

1. Build deterministic AlphaEarth, Sentinel-2, and DEM artifacts, attach all three to one research
   record, destroy/recreate the manager, and require byte-stable structured brief content.
2. Repeat with Sentinel failure and require a usable partial brief that names the missing source and
   retains the other citations.
3. Run the complete rebuilt offline suite, science-off contracts, bundle audits, exit guards, terrain,
   3D Tiles, manipulator, photo/AI, satellite, panel, and query-consumer tests.
4. Scan production research code for camera/scene mutation calls, network listeners, raw payload
   serialization, repository writes, and unsafe path construction; the scan must be empty.
5. Record exact commands, pass counts, source semantics, persistence root contract, and known
   limitations. Commit: `test(scienceearth): verify persistent research evidence`.

## Task 7: Build once and update the fixed Desktop application

1. Start from a clean committed source. Build/install Release without launching the app.
2. Package the existing formal name and bundle id only:
   `/Users/USER/Desktop/osgSol Earth.app`, `com.anloren.osgsol.earth`.
3. Back up the previous app outside Desktop, update the fixed app atomically, keep `manual-test`, and
   record the exact source commit and ScienceEarth version boundary. Do not create a sibling app.
4. Run static bundle audit, dependency audit, code-sign verification, executable `--help`/non-GUI
   contract only if it is proven not to create a crash report, and compare pre/post `.ips` inventory.
   Never foreground or open the app.
5. Deliver one final manual acceptance list covering DEM semantics/display, three-source Agent brief,
   camera independence, prior AlphaEarth/Sentinel behavior, Hong Kong/terrain/3D Tiles, photo/ISS,
   clean preset, panel scrolling, and normal Quit. Manual Quit remains user-run.

## G3 completion gate

The goal is complete only when persistent research survives restart, a three-source cited brief is
scientifically bounded, partial failure is honest, all automated preservation gates pass, and the
single fixed Desktop app is ready for one final user acceptance. Formal tags and synchronization
wait for that acceptance unless the user explicitly requests them earlier.

