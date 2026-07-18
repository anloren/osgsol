# ScienceEarth G3 Persistent Research and Evidence Design

## Outcome

Turn the existing single-source Agent tools into a persistent, multi-source research workflow that
can combine AlphaEarth, Sentinel-2, and Copernicus DEM evidence without granting the research layer
camera or scene authority. G3 produces restart-safe research records, compact evidence documents,
and scientifically bounded cited briefs. It does not add an autonomous web crawler, a local server,
or an unbounded model-controlled data pipeline.

## What already exists and remains authoritative

G1/G2 and the delivered 64D analysis work already provide:

- `ScienceSourceRegistry` discovery;
- normalized `GeoTemporalQuery` contracts;
- asynchronous provider jobs through `ScienceQueryService`;
- bounded in-memory `ScienceArtifactStore` retention;
- exact AlphaEarth 64D metrics/PCA/clustering and limitations;
- Sentinel-2 scene/time/cloud/COG evidence;
- Agent tools for source search, query start/poll, explicit display, compatible artifact comparison,
  and regional change;
- no implicit camera authority.

G3 must extend these contracts. It must not duplicate the registry, query service, or 64D engine.

## Persistent model

`ScienceResearchManager` owns durable research records while `ScienceQueryService` continues to own
live provider execution.

```cpp
enum class ScienceResearchState { Draft, Running, Partial, Ready, Failed };

struct ScienceResearchStep
{
    std::uint64_t liveJobId = 0;
    std::string sourceId;
    ScienceJobState state = ScienceJobState::Idle;
    std::string artifactId;
    std::string message;
};

struct ScienceResearchRecord
{
    std::string researchId;
    std::string question;
    ScienceResearchState state = ScienceResearchState::Draft;
    std::vector<ScienceResearchStep> steps;
    std::string createdAt;
    std::string updatedAt;
};
```

The manager creates a stable `research-<UTC>-<counter>` id, registers each source query as a step,
observes job snapshots during polling, records terminal failure/cancellation, and attaches a compact
evidence document when an artifact becomes ready. A research record becomes:

- `Running` while any step is live;
- `Ready` when all terminal steps succeeded and at least one evidence record exists;
- `Partial` when ready evidence exists alongside failure/cancellation;
- `Failed` only when all terminal steps lack evidence.

Live integer query-job ids remain session-scoped. Stable research ids and evidence survive restart.

## Evidence Store

`ScienceEvidenceStore` persists under:

`~/Library/Application Support/osgSol Earth/research/`

Tests inject a temporary root. The store contains only compact JSON evidence and research manifests;
it never serializes RGBA pixels, 64D cell arrays, masks, or scene textures.

Each evidence document records:

- artifact id, source id/name/version, dataset id, original URL, attribution;
- requested and actual WGS84 coverage;
- variables and original units;
- acquisition/publication/forecast times as distinct fields;
- numeric scalar summaries and bounded primary metrics;
- valid/NoData counts and actual/source/display resolution;
- processing version and ordered processing steps;
- warnings, scientific interpretations supplied by the deterministic engine, and limitations;
- a schema version and creation timestamp.

Writes use a temporary file plus atomic rename. Research ids never become unchecked paths. The
implementation rejects symlink escapes, malformed/oversized files, unknown schema versions, duplicate
ids with incompatible content, non-finite numbers, excessive list sizes, and evidence documents over
1 MiB. A saved research package is independent of the transient science raster cache.

## Brief builder

`ScienceResearchBriefBuilder` is deterministic and data-bounded. It accepts one research record and
its stored evidence, then returns both structured sections and Markdown:

- scope and status;
- **observations** tied to one or more citations;
- **cross-source inferences** explicitly labelled as inference;
- source/time/coverage table;
- processing and limitations;
- numbered citations with dataset/version/URL/attribution.

Scientific rules:

- AlphaEarth distances are latent-representation differences, not named physical variables or
  causal explanations.
- Sentinel-2 natural-color evidence describes the selected acquisition and scene-wide cloud field;
  it is not automatically a cloud-free composite or proof of change.
- Copernicus DEM describes a static EGM2008 DSM; it is not bare-earth terrain and cannot explain
  temporal change.
- Cross-source synthesis is emitted only for overlapping actual coverage. Temporal mismatch is
  reported, not hidden.
- A material statement with no evidence id/citation is omitted.
- Provider failure yields an explicitly partial brief; successful sources remain usable.
- The builder never calls an LLM and never fabricates a domain label from 64D components.

## Agent tool compatibility

Keep the existing tool names and behavior. Extend them as follows:

### `start_science_research`

Optional `research_id` attaches a query to an existing record. Optional `research_question` creates
a persistent record when no id is supplied. Legacy calls with neither remain transient and produce
the same query/camera/layer behavior as v0.5.0.

### `get_research_job`

Legacy integer `job_id` still polls the live query. Optional `research_id` returns the durable
multi-step record and evidence ids. Polling a live step updates the persistent record. The response
never includes raw rasters or embeddings.

### `build_research_brief`

Requires `research_id`. Returns structured observations, explicitly labelled inferences,
limitations, source table, citations, and Markdown. It accepts partial research and reports missing
or failed steps. It never changes the view or displays a layer.

`search_science_sources`, `compare_science_artifacts`, `run_change_analysis`, and
`show_science_artifact` remain compatible. Explicit `show_science_artifact` is still the only
science research operation that may change science-layer visibility, and it still cannot move the
camera.

## UI/UE

The normal ScienceEarth operation/result panels remain source-focused. Persistent research is
primarily an Agent/chat workflow and must not add a third dense permanent panel. The result card may
show a single compact "saved to research" line and stable research id only when the current artifact
is attached. Full evidence and explanations remain behind the existing detail/help affordances or in
the generated brief.

Errors use actionable categories: unavailable source, no coverage, cancelled, evidence not yet
ready, partial research, incompatible evidence, or corrupted saved record. Generic "source degraded"
is not a substitute for the actual failure in the brief.

## Boundaries

- No scene or camera pointer exists in `ScienceResearchManager`, `ScienceEvidenceStore`, or the brief
  builder.
- No local listener, HTTP service, macOS setting change, crash suppression, or keychain mutation.
- Default persistence is a normal app-owned data directory; automated tests use only temporary roots.
- Research persistence never writes inside the signed app bundle or the source repository.
- Science-off build remains independent and preserves prior behavior.
- Saved evidence can be read after restart; saved rasters are not promised until a separate explicit
  payload-cache design.

## Completion gates

- Round-trip and corruption tests for research/evidence persistence.
- Restart test: create, store three-source evidence, reconstruct manager, and build the same brief.
- Partial source failure and cancellation tests.
- Citation coverage test: every observation/inference carries valid evidence ids.
- Scientific-language tests for AlphaEarth, Sentinel-2, and DEM limitations.
- Agent compatibility tests for all existing schemas and camera/layer invariants.
- Cross-source integration using deterministic artifacts from all three providers.
- Full offline, science-off, package audit, and no-GUI preservation gates.

