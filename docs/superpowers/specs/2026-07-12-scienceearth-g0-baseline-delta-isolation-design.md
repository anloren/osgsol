# ScienceEarth G0 Strict Baseline-Delta Isolation Design

**Date:** 2026-07-12

**Status:** Approved product direction; implementation not yet started

**Protected boundary:** `ScienceEarth` / osgSol Earth `v0.2.0`

## 1. Decision

G0 will replace its impossible all-bundle absolute-isolation comparison with a strict,
identity-based baseline-delta gate. This change recognizes only violations already present in the
immutable `v0.2.0` bundle. It does not authorize ScienceEarth to add dependency leakage to the
main executable or any non-science library.

The isolation decision changes no performance, correctness, camera, cache, memory, or existing
behavior limit. In particular, the uncached first-RGB latency gate remains median `<= 3 s` and P95
`<= 8 s`.

## 2. Context

The hardened G0 audit established all of the following:

- the main executable reaches no GDAL, PROJ, or ZSTD dependency;
- the disposable science plugin adds 20.30 MiB, below the 40 MiB target;
- the unchanged protected bundle already contains 49 ZSTD markers in
  `libosgVerseReaderWriter.so`;
- the unchanged protected bundle accounts for all 572 external resolutions and all 36
  Homebrew/forbidden-prefix findings;
- the candidate science plugin contains the expected private static GDAL/PROJ/ZSTD closure and
  adds 74 source/data-prefix strings that must be classified and bounded;
- NVIDIA and Hong Kong corrected uncached RGB medians still exceed 3 seconds.

Rebuilding the complete historical bundle solely to erase pre-existing findings would touch
stable TIFF, reader/writer, packaging, and runtime paths before any scientific product behavior is
delivered. Ignoring isolation entirely would allow new contamination to accumulate. A strict
baseline-delta gate preserves the existing product while keeping ScienceEarth additions bounded
and reversible.

## 3. Isolation model

### 3.1 Immutable reference

The reference bundle is produced from the immutable `ScienceEarth` tag, which resolves to
`0e91c7c4b121d80b929d595ea711d3dd0833ee67`. The tag must never move.

The remediation will commit two normalized manifests containing finding identities, not just
aggregate counts:

- an immutable reference manifest generated from the protected boundary;
- a ratchet manifest initialized from that reference and advanced only by a verified formal
  release when historical findings have been removed.

Each identity includes:

- owning Mach-O path relative to the app bundle;
- finding class;
- referenced install name, canonical forbidden path, symbol, or marker;
- owning architecture where relevant.

Each manifest records its schema version, audit-tool version, source commit, bundle fingerprint,
compiler/SDK metadata, and generation command. The ratchet manifest also records its parent
manifest and approved release tag. A missing, malformed, mismatched, or silently regenerated
manifest is a hard `STOP`.

### 3.2 Two isolation tiers

**Tier A: absolute ScienceEarth runtime isolation**

The ScienceEarth closure must satisfy all of these conditions without comparison to historical
debt:

- no non-system dynamic dependency outside the app bundle;
- no `/opt/homebrew`, `/usr/local`, source-tree, or build-tree runtime lookup;
- no unresolved install name;
- no main-executable or non-science-library link edge to GDAL, PROJ, or the private science ZSTD;
- no external GDAL plugin discovery;
- only the pinned, trimmed GDAL/PROJ/ZSTD capabilities are present;
- plugin removal or initialization failure leaves the existing app operational.

Static GDAL/PROJ/ZSTD symbols are permitted only inside the science plugin, hidden from consumers,
and bound to the verified pinned archives. Required compiled provenance/data-prefix strings must
be removed where practical. Any remaining non-runtime string needs an exact allowlist identity,
an explanation of why it cannot be resolved or opened at runtime, and a test proving the claim.

**Tier B: zero-delta protection for the historical non-science bundle**

For the main executable and every non-science Mach-O, the candidate finding-identity set must be a
subset of the current ratchet manifest. The current ratchet is itself constrained to be a subset
of the immutable reference. Count equality alone is insufficient: one removed finding cannot hide
a different newly introduced finding.

Existing findings may disappear. They may not move to another binary, change target, grow in
scope, or be replaced by a different finding. At formal release, the accepted candidate identity
set becomes the next ratchet ceiling. Later changes cannot restore removed debt without explicit
product review and a separately recorded exception; the immutable reference never changes.

## 4. Dependency and module boundary

The private science runtime remains one dependency island:

- the main app talks to it through the existing science service/plugin boundary;
- existing OSG TIFF aliases, terrain, 3D Tiles, camera, photo, and media paths remain authoritative;
- future scientific providers reuse this runtime rather than embedding independent GDAL/ZSTD
  copies in multiple modules;
- provider-specific code may depend on the science API, but the science API must not expose GDAL,
  PROJ, or ZSTD types;
- AI and Agent tools operate through science query/job/artifact/evidence interfaces, so dependency
  isolation does not reduce cross-source research or orchestration capability.

This keeps ScienceEarth replaceable, allows a science-off build, and prevents dependency ABI
choices from spreading into established application code.

## 5. Audit and acceptance flow

Every candidate audit performs these steps in order:

1. verify that the immutable tag, reference manifest, and ratchet chain identify the approved
   boundary and accepted releases;
2. build or locate the protected baseline and candidate with recorded equivalent settings;
3. recursively enumerate every Mach-O and resolve dependencies in real dyld runpath order;
4. split the graph into science and non-science ownership closures;
5. apply Tier A absolute checks to the science closure;
6. compare Tier B normalized identities against the current ratchet ceiling and verify that the
   ratchet remains a subset of the immutable reference;
7. reject any unknown owner, new identity, unresolved edge, or baseline mismatch;
8. emit machine-readable JSON and a human-readable delta report;
9. run science-off, plugin-absent, private-prefix, dependency-hash, size, correctness, range,
   memory, camera, cache, and latency gates;
10. record `GO` only after automated evidence and explicit human product sign-off pass.

The report must display absolute totals and deltas together. Baseline-relative acceptance must
never make historical debt invisible.

## 6. Clean-machine and failure behavior

Baseline-delta isolation proves that ScienceEarth does not worsen the protected product; it does
not prove that historical packaging debt is harmless. Therefore every formal ScienceEarth package
also needs a clean-machine launch smoke test with Homebrew paths unavailable.

Failures are handled conservatively:

- audit/tool/schema mismatch: `STOP`, never auto-rebaseline;
- new non-science finding: `STOP` with owner and identity;
- science runtime external lookup or symbol leakage: `STOP`;
- science plugin missing or rejected at runtime: app starts, science is unavailable/partial;
- existing baseline debt causing clean-machine failure: release is blocked and the debt is fixed in
  a separate, regression-tested baseline-cleanup change.

## 7. Performance remains a separate gate

This decision resolves only how isolation is measured. It does not turn the current G0 result into
`GO` because corrected uncached first-RGB latency still fails the approved median limit.

Latency remediation must be measured independently and must preserve:

- correct NVIDIA and Hong Kong tile/year selection;
- exact A01/A16/A09 signed dequantization and vertical orientation;
- bounded byte-range access with no complete COG download;
- existing memory and cache limits;
- no camera mutation.

The isolation report and latency report remain separate evidence sections so improvement in one
cannot mask regression in the other.

## 8. Release and maintenance impact

- Normal runtime behavior incurs no direct cost from the delta policy.
- Builds and releases gain a reproducible baseline-comparison step.
- Compiler, SDK, architecture, dependency, or bundle-layout changes may require an explicit
  reviewed baseline migration; they never rewrite the immutable `ScienceEarth` boundary.
- Historical absolute debt remains visible in reports and is tracked separately.
- A future full cleanup can reduce the baseline ceiling without changing the science API.
- Formal releases retain paired immutable tags `vX.Y.Z` and `ScienceEarth-vX.Y.Z` only after all
  gates pass.

## 9. Alternatives rejected

### 9.1 Clean the complete historical bundle before G1

This produces the strongest absolute result but expands G0 into unrelated reader/writer and
packaging refactors. It carries the greatest risk of breaking already accepted Earth behavior and
delays the scientific vertical slice.

### 9.2 Ignore all-bundle findings

This is the smallest short-term effort but provides no enforceable boundary. New dependencies,
paths, or symbols could leak into established modules without detection. It is rejected.

### 9.3 Count-only baseline comparison

This can pass when one old finding is removed and a different one is introduced. Identity-based
comparison is required instead.

## 10. Completion criteria for the G0 remediation plan

The follow-on implementation plan is complete only when:

- the reference manifest is immutable, normalized, and independently reproducible;
- the ratchet manifest is chained, reviewable, and cannot restore removed findings silently;
- Tier A science isolation passes;
- Tier B candidate-minus-baseline identity delta is empty;
- absolute debt and delta are both visible in reports;
- all existing private dependency, size, correctness, and bounded-range proofs still pass;
- science-off and plugin-absent behavior pass unchanged;
- uncached RGB latency meets the existing hard limits;
- the updated G0 record contains explicit human product sign-off before Tasks 7-16 begin.
