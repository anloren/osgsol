#include "science_workbench_model.h"
#include "science_temporal_adapter.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
bool finitePoint(const earthscience::ScienceWgs84Point& point)
{
    return std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
           point.latitude >= -90.0 && point.latitude <= 90.0 &&
           point.longitude >= -180.0 && point.longitude <= 180.0;
}

bool validGeometry(const earthscience::ScienceGeometry& geometry)
{
    if (geometry.kind == earthscience::ScienceGeometryKind::Point)
        return finitePoint(geometry.point);
    const earthscience::ScienceWgs84Bounds& bounds = geometry.bounds;
    return std::isfinite(bounds.west) && std::isfinite(bounds.south) &&
           std::isfinite(bounds.east) && std::isfinite(bounds.north) &&
           bounds.south >= -90.0 && bounds.north <= 90.0 &&
           bounds.south < bounds.north && bounds.west < bounds.east;
}

bool sameGeometry(const earthscience::ScienceGeometry& lhs,
                  const earthscience::ScienceGeometry& rhs)
{
    if (lhs.kind != rhs.kind) return false;
    if (lhs.kind == earthscience::ScienceGeometryKind::Point)
        return lhs.point.latitude == rhs.point.latitude &&
               lhs.point.longitude == rhs.point.longitude &&
               lhs.requestedSpanMeters == rhs.requestedSpanMeters;
    return lhs.bounds.west == rhs.bounds.west &&
           lhs.bounds.south == rhs.bounds.south &&
           lhs.bounds.east == rhs.bounds.east &&
           lhs.bounds.north == rhs.bounds.north &&
           lhs.requestedSpanMeters == rhs.requestedSpanMeters;
}

earthscience::ScienceWgs84Point geometryCenter(
    const earthscience::ScienceGeometry& geometry)
{
    if (geometry.kind == earthscience::ScienceGeometryKind::Point)
        return geometry.point;
    return {(geometry.bounds.south + geometry.bounds.north) * 0.5,
            (geometry.bounds.west + geometry.bounds.east) * 0.5};
}

bool running(ScienceWorkbenchPhase phase)
{
    return phase == ScienceWorkbenchPhase::Queued ||
           phase == ScienceWorkbenchPhase::Fetching ||
           phase == ScienceWorkbenchPhase::Analyzing;
}

bool validTimeSelection(const earthscience::ScienceTimeSelection& time)
{
    switch (time.mode)
    {
    case earthscience::ScienceTimeMode::ExplicitYears:
        return !time.explicitYears.empty();
    case earthscience::ScienceTimeMode::Interval:
        return !time.intervalStart.empty() && !time.intervalEnd.empty();
    case earthscience::ScienceTimeMode::Instant:
        return !time.instant.empty();
    }
    return false;
}
}

void ScienceWorkbenchModel::configureTemporalSources(
    const std::vector<earthscience::ScienceSourceDescriptor>& sources)
{
    for (const earthscience::ScienceSourceDescriptor& source : sources)
    {
        std::unique_ptr<earthproject::IEarthTemporalAdapter> adapter =
            makeScienceTemporalAdapter(source);
        if (!adapter) continue;
        std::string error;
        _temporalController.registerAdapter(std::move(adapter), error);
    }
    syncTemporalView();
    changed();
}

void ScienceWorkbenchModel::updateLiveCameraContext(
    const earthscience::ScienceGeometry& mapCenterPoint,
    const earthscience::ScienceGeometry& visibleBounds)
{
    if (validGeometry(mapCenterPoint))
    {
        _liveMapCenter = mapCenterPoint;
        _hasLiveMapCenter = true;
    }
    if (validGeometry(visibleBounds))
    {
        _liveVisibleBounds = visibleBounds;
        _hasLiveVisibleBounds = true;
    }
    if (!_view.target.locked && _hasLiveMapCenter &&
        !sameGeometry(_view.draft.geometry, _liveMapCenter))
    {
        _view.draft.geometry = _liveMapCenter;
        _view.target.requested = _liveMapCenter;
        _view.target.requestedCenter = geometryCenter(_liveMapCenter);
        changed();
    }
}

bool ScienceWorkbenchModel::dispatch(
    const ScienceWorkbenchAction& action, std::string& error)
{
    error.clear();
    switch (action.kind)
    {
    case ScienceWorkbenchActionKind::SelectSource:
        if (action.sourceId.empty())
            return fail("workbench-source-required", error);
        if (running(_view.phase))
            return fail("workbench-run-active", error);
        _view.draft.sourceId = action.sourceId;
        _view.selectedMethodId.clear();
        _activeTemporalToken = {};
        syncTemporalView();
        refreshDraftPhase();
        changed();
        return true;

    case ScienceWorkbenchActionKind::SelectAnalysis:
        if (action.analysisId.empty())
            return fail("workbench-analysis-required", error);
        _view.selectedAnalysisId = action.analysisId;
        changed();
        return true;

    case ScienceWorkbenchActionKind::LockMapCenter:
        if (!_hasLiveMapCenter)
            return fail("workbench-map-center-unavailable", error);
        lockTarget(_liveMapCenter);
        return true;

    case ScienceWorkbenchActionKind::LockCurrentView:
        if (!_hasLiveVisibleBounds)
            return fail("workbench-view-unavailable", error);
        lockTarget(_liveVisibleBounds);
        return true;

    case ScienceWorkbenchActionKind::UpdateLockedTarget:
        if (!validGeometry(action.geometry))
            return fail("workbench-target-invalid", error);
        lockTarget(action.geometry);
        return true;

    case ScienceWorkbenchActionKind::SetYearRange:
    {
        if (running(_view.phase))
            return fail("workbench-run-active", error);
        if (action.firstYear <= 0 || action.lastYear <= 0 ||
            action.firstYear > action.lastYear ||
            action.lastYear - action.firstYear > 500)
            return fail("workbench-year-range-invalid", error);
        earthscience::GeoTemporalQuery candidate = _view.draft;
        candidate.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        candidate.time.explicitYears.clear();
        for (int year = action.firstYear; year <= action.lastYear; ++year)
            candidate.time.explicitYears.push_back(year);
        candidate.analysis.baselineYear = action.firstYear;
        candidate.analysis.comparisonYear = action.lastYear;
        if (!updateTemporalRequest(candidate, &error)) return false;
        _view.draft = std::move(candidate);
        refreshDraftPhase();
        changed();
        return true;
    }

    case ScienceWorkbenchActionKind::SetMethod:
        _view.selectedMethodId = action.methodId;
        changed();
        return true;

    case ScienceWorkbenchActionKind::Run:
        if (!_view.target.locked)
            return fail("workbench-target-not-locked", error);
        if (_view.draft.sourceId.empty())
            return fail("workbench-source-required", error);
        if (!validTimeSelection(_view.draft.time))
            return fail("workbench-time-required", error);
        if (running(_view.phase))
            return fail("workbench-run-active", error);
        if (!updateTemporalRequest(_view.draft, &error)) return false;
        if (_activeTemporalToken &&
            !_temporalController.beginLoading(_activeTemporalToken))
            return fail("workbench-temporal-request-stale", error);
        syncTemporalView();
        _pendingSubmission = _view.draft;
        _view.phase = ScienceWorkbenchPhase::Queued;
        _view.progress = {};
        _view.progress.stage = earthscience::ScienceProgressStage::Queued;
        _view.errorCode.clear();
        _view.errorMessage.clear();
        changed();
        return true;

    case ScienceWorkbenchActionKind::Cancel:
        if (!running(_view.phase))
            return fail("workbench-no-active-run", error);
        _pendingSubmission.reset();
        if (_activeTemporalToken)
            _temporalController.markCancelled(_activeTemporalToken);
        syncTemporalView();
        _view.phase = ScienceWorkbenchPhase::Cancelled;
        _view.progress.stage = earthscience::ScienceProgressStage::Cancelled;
        changed();
        return true;

    case ScienceWorkbenchActionKind::OpenReport:
        if (!hasArtifact(action.artifactId))
            return fail("workbench-artifact-not-found", error);
        _view.activeArtifactId = action.artifactId;
        removeMinimized(action.artifactId);
        changed();
        return true;

    case ScienceWorkbenchActionKind::MinimizeReport:
        if (_view.activeArtifactId.empty())
            return fail("workbench-report-not-open", error);
        removeMinimized(_view.activeArtifactId);
        _view.minimizedArtifactIds.push_back(_view.activeArtifactId);
        if (_view.minimizedArtifactIds.size() > 3)
            _view.minimizedArtifactIds.erase(
                _view.minimizedArtifactIds.begin());
        _view.activeArtifactId.clear();
        changed();
        return true;

    case ScienceWorkbenchActionKind::CloseReport:
        if (_view.activeArtifactId.empty())
            return fail("workbench-report-not-open", error);
        _view.activeArtifactId.clear();
        changed();
        return true;

    case ScienceWorkbenchActionKind::RemoveArtifact:
        if (!hasArtifact(action.artifactId))
            return fail("workbench-artifact-not-found", error);
        _artifacts.erase(action.artifactId);
        _view.availableArtifactIds.erase(
            std::remove(_view.availableArtifactIds.begin(),
                        _view.availableArtifactIds.end(), action.artifactId),
            _view.availableArtifactIds.end());
        removeMinimized(action.artifactId);
        if (_view.activeArtifactId == action.artifactId)
            _view.activeArtifactId.clear();
        changed();
        return true;

    case ScienceWorkbenchActionKind::FocusTarget:
        if (!_view.target.locked)
            return fail("workbench-target-not-locked", error);
        return true;

    case ScienceWorkbenchActionKind::SelectMetric:
        if (action.metricId.empty())
            return fail("workbench-metric-required", error);
        _view.selectedMetricId = action.metricId;
        changed();
        return true;

    case ScienceWorkbenchActionKind::SelectYear:
        if (action.selectedYear <= 0)
            return fail("workbench-year-invalid", error);
        _view.selectedYear = action.selectedYear;
        changed();
        return true;
    }
    return fail("workbench-action-unknown", error);
}

std::optional<earthscience::GeoTemporalQuery>
ScienceWorkbenchModel::takePendingSubmission()
{
    std::optional<earthscience::GeoTemporalQuery> result =
        std::move(_pendingSubmission);
    _pendingSubmission.reset();
    return result;
}

void ScienceWorkbenchModel::configureDraft(
    const earthscience::GeoTemporalQuery& draft)
{
    const earthscience::ScienceGeometry lockedGeometry =
        _view.target.requested;
    _view.draft = draft;
    if (_view.target.locked) _view.draft.geometry = lockedGeometry;
    updateTemporalRequest(_view.draft);
    refreshDraftPhase();
    changed();
}

void ScienceWorkbenchModel::applyCost(
    const earthscience::ScienceQueryCost& cost)
{
    _view.cost = cost;
    changed();
}

void ScienceWorkbenchModel::applyProgress(
    const earthscience::ScienceProgress& progress)
{
    _view.progress = progress;
    switch (progress.stage)
    {
    case earthscience::ScienceProgressStage::Queued:
        _view.phase = ScienceWorkbenchPhase::Queued;
        break;
    case earthscience::ScienceProgressStage::Locating:
    case earthscience::ScienceProgressStage::Reading:
    case earthscience::ScienceProgressStage::Decoding:
    case earthscience::ScienceProgressStage::Validating:
    case earthscience::ScienceProgressStage::Aligning:
        _view.phase = ScienceWorkbenchPhase::Fetching;
        break;
    case earthscience::ScienceProgressStage::Analyzing:
    case earthscience::ScienceProgressStage::Materializing:
    case earthscience::ScienceProgressStage::Cancelling:
        _view.phase = ScienceWorkbenchPhase::Analyzing;
        break;
    case earthscience::ScienceProgressStage::Ready:
        _view.phase = ScienceWorkbenchPhase::Ready;
        break;
    case earthscience::ScienceProgressStage::Failed:
        _view.phase = ScienceWorkbenchPhase::Failed;
        break;
    case earthscience::ScienceProgressStage::Cancelled:
        _view.phase = ScienceWorkbenchPhase::Cancelled;
        break;
    case earthscience::ScienceProgressStage::Idle:
        refreshDraftPhase();
        break;
    }
    changed();
}

void ScienceWorkbenchModel::applyJobSnapshot(
    const earthscience::ScienceJobSnapshot& snapshot)
{
    _view.processing = snapshot.processing;
    applyProgress(snapshot.progress);
    switch (snapshot.state)
    {
    case earthscience::ScienceJobState::Queued:
        if (_activeTemporalToken)
            _temporalController.beginLoading(_activeTemporalToken);
        _view.phase = ScienceWorkbenchPhase::Queued;
        break;
    case earthscience::ScienceJobState::Fetching:
        if (_activeTemporalToken)
            _temporalController.beginLoading(_activeTemporalToken);
        if (_view.phase != ScienceWorkbenchPhase::Analyzing)
            _view.phase = ScienceWorkbenchPhase::Fetching;
        break;
    case earthscience::ScienceJobState::Ready:
        _view.phase = ScienceWorkbenchPhase::Ready;
        break;
    case earthscience::ScienceJobState::Failed:
        if (_activeTemporalToken)
            _temporalController.markFailed(
                _activeTemporalToken, snapshot.message);
        _view.phase = ScienceWorkbenchPhase::Failed;
        _view.errorCode = "workbench-run-failed";
        _view.errorMessage = snapshot.message;
        break;
    case earthscience::ScienceJobState::Cancelled:
        if (_activeTemporalToken)
            _temporalController.markCancelled(_activeTemporalToken);
        _view.phase = ScienceWorkbenchPhase::Cancelled;
        _view.errorCode = "workbench-run-cancelled";
        _view.errorMessage = snapshot.message;
        break;
    case earthscience::ScienceJobState::Unavailable:
    case earthscience::ScienceJobState::Idle:
        refreshDraftPhase();
        break;
    }
    if (snapshot.state == earthscience::ScienceJobState::Ready &&
        snapshot.lastSuccessfulAnalysisArtifact &&
        snapshot.lastSuccessfulAnalysisArtifact->generation ==
            snapshot.jobId)
        applyArtifact(snapshot.lastSuccessfulAnalysisArtifact);
    else if (snapshot.displayArtifact)
        applyArtifact(snapshot.displayArtifact);
    else if (snapshot.lastSuccessfulArtifact &&
             !hasArtifact(snapshot.lastSuccessfulArtifact->artifactId))
        applyArtifact(snapshot.lastSuccessfulArtifact);
    syncTemporalView();
    changed();
}

void ScienceWorkbenchModel::applyArtifact(
    std::shared_ptr<const earthscience::ScienceArtifact> artifactValue)
{
    if (!artifactValue || artifactValue->artifactId.empty()) return;
    const std::string id = artifactValue->artifactId;
    const bool isNew = !hasArtifact(id);
    _artifacts[id] = artifactValue;
    if (isNew) _view.availableArtifactIds.push_back(id);
    _view.activeArtifactId = id;
    removeMinimized(id);
    _view.phase = ScienceWorkbenchPhase::Ready;
    _view.errorCode.clear();
    _view.errorMessage.clear();

    const earthproject::EarthTemporalSelection appliedTime =
        earthTemporalSelectionForArtifact(*artifactValue);
    const earthproject::EarthTemporalState* temporalState =
        _temporalController.state(artifactValue->query.sourceId);
    if (temporalState && temporalState->hasRequested &&
        sameTemporalSelection(
            temporalState->requested,
            earthTemporalSelectionForQuery(artifactValue->query)))
    {
        const earthproject::TemporalRequestToken token = {
            artifactValue->query.sourceId, temporalState->generation};
        _temporalController.markApplied(token, appliedTime);
    }

    _view.target.requested = artifactValue->query.geometry;
    _view.target.requestedCenter = geometryCenter(
        artifactValue->query.geometry);
    _view.target.locked = true;
    if (!artifactValue->sourceReferences.empty())
    {
        const earthscience::ScienceWgs84Bounds coverage =
            artifactValue->sourceReferences.front().actualCoverage;
        earthscience::ScienceGeometry actual;
        actual.kind = earthscience::ScienceGeometryKind::BoundingBox;
        actual.bounds = coverage;
        if (validGeometry(actual))
        {
            _view.target.actualCoverage = actual;
            _view.target.returnedCenter = geometryCenter(actual);
            _view.target.hasActualCoverage = true;
        }
    }
    syncTemporalView();
    changed();
}

bool ScienceWorkbenchModel::hasArtifact(const std::string& artifactId) const
{
    return _artifacts.find(artifactId) != _artifacts.end();
}

std::shared_ptr<const earthscience::ScienceArtifact>
ScienceWorkbenchModel::artifact(const std::string& artifactId) const
{
    const auto found = _artifacts.find(artifactId);
    return found == _artifacts.end() ? nullptr : found->second;
}

bool ScienceWorkbenchModel::fail(
    const std::string& code, std::string& error)
{
    error = code;
    return false;
}

void ScienceWorkbenchModel::changed()
{
    ++_view.revision;
}

void ScienceWorkbenchModel::refreshDraftPhase()
{
    if (running(_view.phase)) return;
    if (!_view.target.locked)
        _view.phase = ScienceWorkbenchPhase::Draft;
    else if (!_view.draft.sourceId.empty() &&
             validTimeSelection(_view.draft.time))
        _view.phase = ScienceWorkbenchPhase::ReadyToRun;
    else
        _view.phase = ScienceWorkbenchPhase::TargetLocked;
}

bool ScienceWorkbenchModel::updateTemporalRequest(
    const earthscience::GeoTemporalQuery& query,
    std::string* errorCode)
{
    if (!_temporalController.state(query.sourceId))
    {
        _activeTemporalToken = {};
        syncTemporalView();
        return true;
    }
    const earthproject::TemporalRequestResult result =
        _temporalController.request(
            query.sourceId, earthTemporalSelectionForQuery(query));
    if (!result.ok)
    {
        if (errorCode) *errorCode = result.errorCode;
        syncTemporalView();
        return false;
    }
    _activeTemporalToken = result.token;
    syncTemporalView();
    return true;
}

void ScienceWorkbenchModel::syncTemporalView()
{
    const earthproject::EarthTemporalState* state =
        _temporalController.state(_view.draft.sourceId);
    if (state)
        _view.temporal = *state;
    else
    {
        _view.temporal = {};
        _view.temporal.adapterId = _view.draft.sourceId;
    }
}

void ScienceWorkbenchModel::lockTarget(
    const earthscience::ScienceGeometry& geometry)
{
    _view.target.requested = geometry;
    _view.target.requestedCenter = geometryCenter(geometry);
    _view.target.locked = true;
    _view.target.hasActualCoverage = false;
    _view.draft.geometry = geometry;
    refreshDraftPhase();
    changed();
}

void ScienceWorkbenchModel::removeMinimized(const std::string& artifactId)
{
    _view.minimizedArtifactIds.erase(
        std::remove(_view.minimizedArtifactIds.begin(),
                    _view.minimizedArtifactIds.end(), artifactId),
        _view.minimizedArtifactIds.end());
}
