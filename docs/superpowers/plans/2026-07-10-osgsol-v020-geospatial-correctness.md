# osgSol v0.2.0 Wave 1 Batch 2: Geospatial Correctness

> **Execution note:** Continue only in
> `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on
> `codex/v0.2-runtime-safety`. Do not modify the stable checkout, the old
> `osgverse` repository, or `master`. Do not package the Desktop app or create
> `v0.2.0`; those remain Wave 1 Batch 6 deliverables.

**Goal:** Make satellite and flight picking use the exact positions rendered in
the current frame, preserve view coverage across the international date line,
deduplicate split OpenSky results by aircraft identity, and allow failed
precise-satellite groups to be retried by toggling the category.

**Baseline:** `5a034db929a77f4aaf39f3765dfe6ed3678dfa34`

**Process amendment:** The approved design's original phrase “one commit per
batch” conflicted with the already reviewed Batch 1 task-by-task workflow. The
design now records the actual invariant: one contiguous commit series per
batch, one independently reviewed commit per verifiable task, and a final
verification-record commit. No batch is merged or tagged independently.

**Architecture:** Pure, deterministic geospatial helpers own longitude
normalization, wrapped-bbox splitting, flight extrapolation, stable-key merge,
and satellite ECEF extrapolation. Runtime render and pick paths must call the
same helpers. Camera handlers publish an unwrapped view interval; only network
transport converts it into one or two legal `[-180, 180]` boxes. AIS keeps an
unwrapped subscribed interval for resubscribe decisions while sending the
split boxes in `BoundingBoxes`.

**Tech stack:** C++17, OpenSceneGraph 3.6.5, CMake/CTest, libhv, picojson,
existing `sat_math` and offline test targets.

---

## Plan preparation checkpoint

- Amend the Wave 1 design to record the already-reviewed task-by-task
  commit/review discipline.
- Commit and push the amended design together with this executable Batch 2 plan
  before changing production code.

These two documents form the planning commit; the push and clean-upstream check
are execution preconditions, not pre-completed implementation checkboxes. Task 1
must start only after that commit is pushed and the worktree is clean.

---

## Global invariants

- Every production behavior change gets a failing behavior or production-wiring
  test before implementation.
- Longitude transport boxes always satisfy `-180 <= lonMin <= lonMax <= 180`.
- A view crossing `+180/-180` becomes exactly two transport boxes; a global
  view becomes one `[-180, 180]` box.
- Satellite and flight pick projection uses the same pure extrapolation helper
  as the vertex update for the same reference time.
- OpenSky split-query merge is stable and keyed only by non-empty `icao24`.
- AIS continues using MMSI map identity; two subscription boxes must not create
  duplicate rendered ships.
- Category toggle retry is user-driven only. This batch does not introduce a
  periodic CelesTrak retry loop.
- Feed preservation, dynamic tiles/input, AI Web safety, and final release work
  stay out of this plan.

---

## Task 1: Add deterministic geospatial helper seams

**Files:**

- Create: `applications/earth_explorer/geo_bbox.h`
- Create: `applications/earth_explorer/flight_math.h`
- Create: `applications/earth_explorer/flight_math.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`
- Modify: `applications/earth_explorer/sat_math.h`
- Modify: `applications/earth_explorer/sat_math.cpp`
- Create: `tests/geospatial_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

```cpp
namespace earthgeo
{
    struct GeoBBox
    {
        double latMin = -85.0, lonMin = -180.0;
        double latMax = 85.0, lonMax = 180.0;
    };

    // geo_bbox.h is intentionally header-only. Every definition is inline so
    // EarthExplorer, Geospatial tests, and AIS tests share one implementation
    // without a separate linkage target or ODR violations.
    inline double normalizeLongitude(double lonDeg);
    inline double unwrapLongitudeNear(double lonDeg, double referenceDeg);
    inline double longitudeSpan(const GeoBBox& bbox);
    inline GeoBBox inflateUnwrappedBBox(const GeoBBox& bbox, double factor);
    inline std::vector<GeoBBox> splitAntimeridianBBox(const GeoBBox& bbox);
    inline bool unwrappedBBoxNeedsRefresh(const GeoBBox& subscribed,
                                          const GeoBBox& view);
}

namespace earthflight
{
    struct FlightTrack
    {
        std::string icao24, callsign, country;
        double lon = 0.0, lat = 0.0, altM = 0.0;
        double velMS = 0.0, headingRad = 0.0;
        osg::Vec3d ecef;
    };

    struct FlightPosition
    {
        double lon = 0.0, lat = 0.0;
        osg::Vec3d ecef;
    };

    FlightPosition extrapolateFlightPosition(const FlightTrack& flight,
                                              double elapsedSeconds);
    std::vector<FlightTrack> mergeFlightsByIcao24(
        const std::vector<FlightTrack>& first,
        const std::vector<FlightTrack>& second);
}

namespace earthsat
{
    osg::Vec3d extrapolateSatelliteEcef(const osg::Vec3d& ecef,
                                        const osg::Vec3d& velocity,
                                        double lastUpdateRefTime,
                                        double refTime);
    bool shouldRequestPreciseRefetch(bool enabling, bool fetchDone,
                                     bool categoryHasData);
}
```

- [ ] **Step 1: Add failing pure behavior tests**

Create `tests/geospatial_tests.cpp` and register
`osgVerse_Test_Geospatial` as an `offline` CTest. Follow the existing satellite
test seam by including `flight_math.cpp` directly in the test translation unit;
also add `flight_math.cpp` to EarthExplorer's `EXECUTABLE_FILES`. At registration
time, give the target the existing
`OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"` compile definition so later runtime
wiring tests work from every fresh task commit. Cover at least:

```cpp
// Ordinary view stays one box.
CHECK(splitAntimeridianBBox({20.0, 110.0, 30.0, 120.0}).size() == 1);

// Eastbound crossing: [174,184] -> [174,180] + [-180,-176].
std::vector<GeoBBox> east = splitAntimeridianBBox({-10.0, 174.0, 10.0, 184.0});
CHECK(east.size() == 2);
CHECK(east[0].lonMin == 174.0 && east[0].lonMax == 180.0);
CHECK(east[1].lonMin == -180.0 && east[1].lonMax == -176.0);

// Explicit wrapped form is also accepted.
std::vector<GeoBBox> wrapped =
    splitAntimeridianBBox({-10.0, 179.0, 10.0, -179.0});
CHECK(wrapped.size() == 2);

// Westbound crossing and global coverage.
CHECK(splitAntimeridianBBox({-10.0, -184.0, 10.0, -174.0}).size() == 2);
std::vector<GeoBBox> global =
    splitAntimeridianBBox({-85.0, -180.0, 85.0, 180.0});
CHECK(global.size() == 1 && global[0].lonMin == -180.0 &&
      global[0].lonMax == 180.0);

// Every emitted transport box is legal and their total span equals the input.
```

Also test:

- `unwrapLongitudeNear(-179, 179) == 181` and the reverse direction;
- exact one-box results ending at `-180` or `180` without zero-width fragments;
- alternate globals `0..360` and spans greater than 360 both collapse to one
  exact `[-180,180]` box;
- explicit wrapped `179..-179` emits exact boxes and preserves a 2-degree span;
- inflating that wrapped view produces one continuous unwrapped interval;
- an inflated subscription such as `171.5..186.5` accepts equivalent views
  centered near either `179` or `-179` without a false 358-degree jump, while
  genuinely moved, wider, and quarter-span-narrower views request refresh;
- flight extrapolation at zero, positive, and negative elapsed time;
- stable merge preserves first occurrence order and removes duplicate
  non-empty `icao24` values;
- satellite ECEF extrapolation clamps negative elapsed to zero;
- precise refetch is true only for `enabling && fetchDone && !categoryHasData`.

Append corresponding satellite helper tests to `tests/satellite_tests.cpp` so
the existing satellite target owns its ECEF contract too.

- [ ] **Step 2: Run and verify RED**

```bash
cmake --build build/osgsol_core --target \
  osgVerse_Test_Geospatial osgVerse_Test_Satellite -j2
```

Expected: compilation or linking fails because the new helpers and test target
inputs do not exist.

- [ ] **Step 3: Implement only the pure helpers**

Implementation requirements:

- `normalizeLongitude()` returns a canonical value in `[-180, 180)`.
- `splitAntimeridianBBox()` accepts either an unwrapped interval (`174..184`)
  or explicit wrapped form (`179..-179`), clamps latitude to `[-85,85]`, and
  emits one or two ordered legal transport boxes.
- Input spans `>=360` emit exactly one global box.
- Endpoints exactly at `-180` or `180` remain legal and never create an empty
  second box.
- `inflateUnwrappedBBox()` preserves the logical center and may intentionally
  return longitudes outside `[-180,180]`; it must not discard the wrapped side.
- `unwrappedBBoxNeedsRefresh()` compares the new center after unwrapping it near
  the subscribed center, then retains the existing center-outside, zoom-out,
  and quarter-span zoom-in rules.
- Flight extrapolation uses the existing local-plane formula, clamps elapsed to
  zero, normalizes the returned display longitude, and produces ECEF from the
  returned lat/lon.
- Merge ignores empty `icao24` for dedup purposes but keeps such entries in
  stable order; non-empty duplicates keep the first occurrence.
- Satellite ECEF extrapolation is exactly `ecef + velocity * max(0, dt)`.

- [ ] **Step 4: Verify GREEN and commit**

```bash
cmake --build build/osgsol_core --target \
  osgVerse_Test_Geospatial osgVerse_Test_Satellite -j2
build/osgsol_core/bin/osgVerse_Test_Geospatial
build/osgsol_core/bin/osgVerse_Test_Satellite
git diff --check
git add applications/earth_explorer/geo_bbox.h \
  applications/earth_explorer/flight_math.h \
  applications/earth_explorer/flight_math.cpp \
  applications/earth_explorer/CMakeLists.txt \
  applications/earth_explorer/sat_math.h \
  applications/earth_explorer/sat_math.cpp \
  tests/geospatial_tests.cpp tests/satellite_tests.cpp tests/CMakeLists.txt
git commit -m "test: add geospatial correctness seams"
```

---

## Task 2: Make satellite render, pick, and precise retry share owner state

**Files:**

- Modify: `applications/earth_explorer/sat_data.cpp`
- Modify: `tests/satellite_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- `SatelliteLayerImpl::pickAt(..., double refTime)` receives the current viewer
  reference time.
- `interpolateOne()` and `pickAt()` both call
  `earthsat::extrapolateSatelliteEcef()`.
- Add `std::atomic<bool> _preciseRefetchRequested` and
  `takePreciseRefetchRequest()` mirroring the existing Starlink request.

- [ ] **Step 1: Add a failing production-wiring regression**

Give `osgVerse_Test_Satellite` the existing
`OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"` convention and inspect function
bodies, not global token presence. Every source read must be asserted non-empty.
Assert:

- `interpolateOne()` passes each satellite's `ecef`, `ecefVelocity`,
  `lastUpdateRefTime`, and the current `refTime` to
  `extrapolateSatelliteEcef`;
- `pickAt()` passes the same four values to the same helper before
  hemisphere/projected-distance tests and does not project raw `sat.ecef` or
  substitute a constant/stale time;
- `SatPickHandler` passes `FrameStamp::getReferenceTime()`;
- `setCategoryEnabled()` calls `shouldRequestPreciseRefetch` for Station,
  Navigation, and Weather;
- `FetchThread::run()` consumes `takePreciseRefetchRequest()` before its
  `!preciseFetchedOnce` fetch guard.

Run the satellite target and confirm the binary fails on the first missing
runtime-wiring assertion.

- [ ] **Step 2: Wire render and pick to one position function**

Change `interpolateOne()` to write:

```cpp
(*va)[i] = earthsat::extrapolateSatelliteEcef(
    sats[i].ecef, sats[i].ecefVelocity,
    sats[i].lastUpdateRefTime, refTime);
```

Change `pickAt()` to calculate `P` with the identical call and the click frame's
`refTime`. Pass the frame stamp time from `SatPickHandler`; fall back to `0.0`
only when no frame stamp exists. Projection, front-hemisphere rejection, and
pixel tolerance remain otherwise unchanged.

- [ ] **Step 3: Add precise-category retry**

When a precise category is toggled on after a completed fetch and that category
has no entry in `_allPrecise`, publish `_preciseRefetchRequested=true`. Do not
request a retry when data for that category already exists.

In `FetchThread::run()`:

```cpp
if (_owner->takePreciseRefetchRequest()) preciseFetchedOnce = false;
```

Place it immediately before the existing precise-fetch guard so the same loop
performs the retry. Keep Starlink retry and both propagation intervals intact.
Update `fetchErrorText()` comments to state that all four categories support a
toggle-off/on retry; do not claim automatic retries.

- [ ] **Step 4: Verify and commit**

```bash
cmake --build build/osgsol_core --target \
  osgVerse_Test_Satellite osgVerse_EarthExplorer -j2
build/osgsol_core/bin/osgVerse_Test_Satellite
ctest --test-dir build/osgsol_core -L offline --output-on-failure
git diff --check
git add applications/earth_explorer/sat_data.cpp \
  tests/satellite_tests.cpp tests/CMakeLists.txt
git commit -m "fix: align satellite picking and retry state"
```

---

## Task 3: Split OpenSky queries and pick extrapolated flights

**Files:**

- Modify: `applications/earth_explorer/flight_data.cpp`
- Modify: `tests/geospatial_tests.cpp`

**Interfaces:**

- Replace the anonymous `Flight` value with `earthflight::FlightTrack`.
- `parseOpenSky()` stores state index `0` as `icao24`.
- The network path calls `splitAntimeridianBBox()` and merges query results with
  `mergeFlightsByIcao24()`.
- `FlightLayerImpl::pickAt(..., double refTime)` and `interpolate()` both call
  `extrapolateFlightPosition()`.

- [ ] **Step 1: Add failing runtime-wiring tests**

Use the `OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"` definition registered in
Task 1, assert every file read is non-empty, and extract the relevant function
bodies. Assert:

- parser assigns `icao24` from OpenSky state index `0`;
- parser rejects a row unless state index `0` is a non-empty string before the
  row reaches `push_back` (lock both the type check and empty-string guard);
- network fetch iterates the actual vector returned by
  `splitAntimeridianBBox`, passes each element's four bounds to the request URL,
  and feeds each returned result into `mergeFlightsByIcao24`;
- fixture mode parses its file once and does not duplicate it per split box;
- `interpolate()` and `pickAt()` both derive clamped
  `elapsed=max(0, refTime-_t0)` and pass the same flight plus that elapsed to
  `extrapolateFlightPosition`;
- pick projection does not use raw `flight.ecef`;
- selected `FlightInfo.lon/lat` comes from the returned `FlightPosition`, not
  snapshot `f.lon/f.lat`;
- `FlightPickHandler` passes the frame stamp reference time;
- `FlightBBoxHandler` no longer clamps `lonMin/lonMax` before publication.

Run `osgVerse_Test_Geospatial` and confirm a wiring assertion fails.

- [ ] **Step 2: Use stable aircraft identity**

Require a valid non-empty string at state index `0` when parsing OpenSky rows.
Store it as `icao24`. Keep the existing airborne, coordinate, altitude,
velocity, heading, callsign, and country filters.

- [ ] **Step 3: Query both sides of the date line**

Keep fixture handling at the top-level fetch function so it reads once.
For network mode:

1. Build an unwrapped `GeoBBox` from the latest view.
2. Split it into one or two legal boxes.
3. Query OpenSky once per box.
4. Merge successful responses in box order by `icao24`.

Never send `lomin > lomax`, or a longitude outside `[-180,180]`. This batch does
not change failure-preserves-data semantics; that belongs to Batch 3.

Remove the final longitude clamp from `FlightBBoxHandler`. Latitude clamps and
the `thetaDeg >= 80` global fallback stay unchanged.

- [ ] **Step 4: Pick the rendered flight position**

Use `elapsed = max(0, refTime - _t0)` and one call to
`extrapolateFlightPosition()` per flight in both vertex update and pick. When a
flight is selected, publish the extrapolated lat/lon in `FlightInfo`, not the
stale snapshot coordinates. Other metadata remains from the same track.

- [ ] **Step 5: Verify and commit**

```bash
cmake --build build/osgsol_core --target \
  osgVerse_Test_Geospatial osgVerse_EarthExplorer -j2
build/osgsol_core/bin/osgVerse_Test_Geospatial
ctest --test-dir build/osgsol_core -L offline --output-on-failure
git diff --check
git add applications/earth_explorer/flight_data.cpp \
  tests/geospatial_tests.cpp
git commit -m "fix: preserve flight coverage across the date line"
```

---

## Task 4: Send two AIS subscription boxes across the date line

**Files:**

- Modify: `applications/earth_explorer/ais_math.h`
- Modify: `applications/earth_explorer/ais_math.cpp`
- Modify: `applications/earth_explorer/ais_data.cpp`
- Modify: `applications/earth_explorer/earth_main.cpp`
- Modify: `tests/ais_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- `ShipBBox` aliases or value-converts `earthgeo::GeoBBox`.
- `inflateBBox()` preserves an unwrapped logical interval.
- `std::vector<ShipBBox> splitSubscriptionBoxes(const ShipBBox&)` is the only
  adapter from unwrapped AIS state to legal transport boxes and delegates to
  `earthgeo::splitAntimeridianBBox()`.
- `buildSubscriptionJson()` accepts the split box vector and writes every box
  into `BoundingBoxes`.
- `_subscribedBBox` remains the unwrapped inflated interval used by
  `bboxNeedsResubscribe()`.

- [ ] **Step 1: Add failing AIS bbox and JSON tests**

Extend `tests/ais_tests.cpp` with:

- normal view -> one subscription box;
- `[174,184]` -> two boxes with legal longitude ordering;
- both boxes appear in JSON in stable order;
- longitude wrapping does not trigger immediate resubscribe for a view that
  moved from `179` to equivalent `-179` inside the inflated subscription;
- a genuinely moved or much wider/narrower view still triggers resubscribe;
- duplicate MMSI fixture messages still produce one logical ship entry (lock
  the existing map-by-MMSI production wiring if a direct behavior seam is not
  available).

Give `osgVerse_Test_Ais` the existing
`OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"` compile definition. Replace the
current hard-coded `/Users/USER/osgverse/.../ais_fixture.jsonl` path with a
path derived from that macro, so this new repository never reads the old one.
Assert fixture/source reads are non-empty.

Add source-wiring assertions that:

- `connectWs()` flows `_subscribedBBox` into `splitSubscriptionBoxes()` and the
  returned vector into `buildSubscriptionJson()`;
- `ShipViewStateHandler` no longer clamps longitude;
- both `onWsMessage()` and fixture loading upsert the same `_store` keyed by
  `mmsi`, rather than maintaining one collection per transport box.

Run `osgVerse_Test_Ais` and confirm RED.

- [ ] **Step 2: Preserve unwrapped subscription state**

Refactor `inflateBBox()` to use `earthgeo::inflateUnwrappedBBox()`. Store that
raw interval in `_subscribedBBox`; pass its split vector only to JSON creation.
Refactor `bboxNeedsResubscribe()` to use
`earthgeo::unwrappedBBoxNeedsRefresh()`.

`connectWs()` must therefore follow:

```cpp
_subscribedBBox = earthais::inflateBBox(view, 1.5);
std::vector<ShipBBox> boxes = earthais::splitSubscriptionBoxes(_subscribedBBox);
std::string sub = earthais::buildSubscriptionJson(_apiKey, boxes);
```

All boxes must use official AISStream coordinate order `[lat, lon]`.

- [ ] **Step 3: Publish the raw camera longitude interval**

Remove only the `[-180,180]` longitude clamp in `ShipViewStateHandler`.
Continue clamping latitude, limiting `lonHalf <= 180`, preserving the high-alt
gate, and publishing `camAltM`.

The existing `_store` map keyed by MMSI remains the deduplication owner;
`_ships` remains only the rendered main-thread vector. Do not introduce a
second per-box ship collection.

- [ ] **Step 4: Verify and commit**

```bash
cmake --build build/osgsol_core --target \
  osgVerse_Test_Ais osgVerse_EarthExplorer -j2
build/osgsol_core/bin/osgVerse_Test_Ais
ctest --test-dir build/osgsol_core -L offline --output-on-failure
git diff --check
git add applications/earth_explorer/ais_math.h \
  applications/earth_explorer/ais_math.cpp \
  applications/earth_explorer/ais_data.cpp \
  applications/earth_explorer/earth_main.cpp \
  tests/ais_tests.cpp tests/CMakeLists.txt
git commit -m "fix: split live view boxes at the date line"
```

---

## Task 5: Batch 2 verification checkpoint

**Files:**

- Modify: `docs/superpowers/plans/2026-07-10-osgsol-v020-geospatial-correctness.md`

- [ ] **Step 1: Rebuild all affected targets**

```bash
cmake --build build/osgsol_core --target \
  osgVerse_Test_Geospatial osgVerse_Test_Satellite osgVerse_Test_Ais \
  osgVerse_Test_Feeds osgVerse_Test_MediaThreading osgVerse_EarthExplorer -j2
```

- [ ] **Step 2: Run the complete offline gate**

```bash
ctest --test-dir build/osgsol_core -L offline --output-on-failure
```

Expected: 100% pass; the total grows by one because of
`osgVerse_Test_Geospatial`.

- [ ] **Step 3: Run a fresh offscreen smoke**

```bash
rm -f /tmp/earth_capture_0.png /tmp/osgsol-v020-geospatial-smoke.png
EARTH_OFFSCREEN=1 EARTH_IME=0 EARTH_AUTOCAP=120 \
  build/osgsol_core/bin/osgVerse_EarthExplorer
mv /tmp/earth_capture_0.png /tmp/osgsol-v020-geospatial-smoke.png
test -s /tmp/osgsol-v020-geospatial-smoke.png
```

Record process exit, context line, image dimensions, byte count, and SHA-256.

- [ ] **Step 4: Run source regressions**

```bash
rg -n 'extrapolateSatelliteEcef|extrapolateFlightPosition|splitAntimeridianBBox|mergeFlightsByIcao24|BoundingBoxes' \
  applications/earth_explorer tests
rg -n 'resolveImGuiWheelAmount|OSGSOL_HAS_PHOTO_REQUEST|_videoRequests.drain' \
  applications/earth_explorer tests
```

Confirm no runtime pick path projects a raw stale snapshot position and no
camera handler clamps the raw longitude interval.

- [ ] **Step 5: Mark completed boxes and commit the record**

```bash
git add docs/superpowers/plans/2026-07-10-osgsol-v020-geospatial-correctness.md
git commit -m "docs: record v0.2 geospatial verification"
```

Do not package, update `/Users/USER/Desktop/osgSol Earth.app`, merge
`master`, or create a tag after this checkpoint. The next approved batch is
data reliability.
