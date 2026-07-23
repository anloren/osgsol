#include "science_workbench_model.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace
{
void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

earthscience::ScienceGeometry point(double latitude, double longitude)
{
    earthscience::ScienceGeometry geometry;
    geometry.kind = earthscience::ScienceGeometryKind::Point;
    geometry.point.latitude = latitude;
    geometry.point.longitude = longitude;
    return geometry;
}

earthscience::ScienceGeometry bounds(double west, double south,
                                     double east, double north)
{
    earthscience::ScienceGeometry geometry;
    geometry.kind = earthscience::ScienceGeometryKind::BoundingBox;
    geometry.bounds = {west, south, east, north};
    return geometry;
}

ScienceWorkbenchAction action(ScienceWorkbenchActionKind kind)
{
    ScienceWorkbenchAction value;
    value.kind = kind;
    return value;
}

void makeRunnable(ScienceWorkbenchModel& model)
{
    ScienceWorkbenchAction source = action(
        ScienceWorkbenchActionKind::SelectSource);
    source.sourceId = "era5-land-agriculture";
    std::string error;
    expect(model.dispatch(source, error), "source selection must succeed");

    model.updateLiveCameraContext(
        point(24.3658, 104.1796), bounds(100.0, 20.0, 108.0, 28.0));
    expect(model.dispatch(
        action(ScienceWorkbenchActionKind::LockMapCenter), error),
        "map center lock must succeed");

    ScienceWorkbenchAction years = action(
        ScienceWorkbenchActionKind::SetYearRange);
    years.firstYear = 2017;
    years.lastYear = 2025;
    expect(model.dispatch(years, error), "year range must succeed");
}

void testTargetLockIsImmutableAcrossCameraMotion()
{
    ScienceWorkbenchModel model;
    makeRunnable(model);
    const earthscience::ScienceGeometry locked =
        model.viewModel().target.requested;
    const std::uint64_t lockedRevision = model.viewModel().revision;

    model.updateLiveCameraContext(
        point(35.6762, 139.6503), bounds(138.0, 34.0, 141.0, 37.0));

    expect(model.viewModel().target.locked, "target must stay locked");
    expect(model.viewModel().target.requested.point.latitude ==
               locked.point.latitude,
           "camera motion must not change locked latitude");
    expect(model.viewModel().target.requested.point.longitude ==
               locked.point.longitude,
           "camera motion must not change locked longitude");
    expect(model.viewModel().draft.geometry.point.latitude ==
               locked.point.latitude,
           "camera motion must not change draft latitude after lock");
    expect(model.viewModel().revision == lockedRevision,
           "camera motion must not publish a new revision after target lock");
}

void testRunRequiresValidFrozenDraftAndEmitsOnce()
{
    ScienceWorkbenchModel model;
    std::string error;
    expect(!model.dispatch(action(ScienceWorkbenchActionKind::Run), error),
           "empty draft must not run");
    expect(error == "workbench-target-not-locked",
           "empty run must report stable target error");

    makeRunnable(model);
    expect(model.viewModel().phase == ScienceWorkbenchPhase::ReadyToRun,
           "complete draft must be ready to run");
    expect(model.dispatch(action(ScienceWorkbenchActionKind::Run), error),
           "valid draft must run");

    const std::optional<earthscience::GeoTemporalQuery> first =
        model.takePendingSubmission();
    const std::optional<earthscience::GeoTemporalQuery> second =
        model.takePendingSubmission();
    expect(first.has_value(), "first submission take must return the query");
    expect(!second.has_value(), "submission must not be returned twice");
    expect(first->geometry.point.latitude == 24.3658,
           "submitted query must use frozen target");
    expect(first->time.explicitYears.front() == 2017 &&
               first->time.explicitYears.back() == 2025,
           "submitted query must use selected year range");
}

void testProviderDraftConfigurationPreservesLockedGeometry()
{
    ScienceWorkbenchModel model;
    makeRunnable(model);
    earthscience::GeoTemporalQuery configured;
    configured.sourceId = "era5-agricultural-climate";
    configured.geometry = point(0.0, 0.0);
    configured.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
    configured.time.explicitYears = {2017, 2018, 2019};
    configured.variables = {"temperature_2m_mean"};
    configured.outputKind = earthscience::ScienceOutputKind::TimeSeries;
    configured.analysis.kind = earthscience::ScienceAnalysisKind::PointSeries;

    model.configureDraft(configured);

    expect(model.viewModel().draft.variables.size() == 1,
           "provider configuration must update variables");
    expect(model.viewModel().draft.geometry.point.latitude == 24.3658,
           "provider configuration must preserve locked latitude");
    expect(model.viewModel().draft.geometry.point.longitude == 104.1796,
           "provider configuration must preserve locked longitude");
}

void testRunAcceptsProviderValidatedIntervalAndInstantTime()
{
    for (earthscience::ScienceTimeMode mode : {
             earthscience::ScienceTimeMode::Interval,
             earthscience::ScienceTimeMode::Instant})
    {
        ScienceWorkbenchModel model;
        makeRunnable(model);
        earthscience::GeoTemporalQuery configured;
        configured.sourceId = mode == earthscience::ScienceTimeMode::Interval
            ? "sentinel-2-l2a" : "copernicus-dem-glo-30";
        configured.geometry = point(24.3658, 104.1796);
        configured.time.mode = mode;
        if (mode == earthscience::ScienceTimeMode::Interval)
        {
            configured.time.intervalStart = "2025-01-01T00:00:00Z";
            configured.time.intervalEnd = "2025-12-31T23:59:59Z";
        }
        else
            configured.time.instant = "2021-01-01T00:00:00Z";
        configured.outputKind = earthscience::ScienceOutputKind::RasterLayer;
        model.configureDraft(configured);

        std::string error;
        expect(model.dispatch(action(ScienceWorkbenchActionKind::Run), error),
               "validated interval or instant draft must run");
        expect(model.takePendingSubmission().has_value(),
               "validated raster draft must emit a submission");
    }
}

std::shared_ptr<const earthscience::ScienceArtifact> artifact(
    const std::string& id, double west, double south,
    double east, double north)
{
    auto value = std::make_shared<earthscience::ScienceArtifact>();
    value->artifactId = id;
    earthscience::ScienceSourceReference reference;
    reference.sourceId = "era5-land-agriculture";
    reference.actualCoverage = {west, south, east, north};
    value->sourceReferences.push_back(reference);
    return value;
}

void testReportLifecycleDoesNotDeleteOnCloseOrMinimize()
{
    ScienceWorkbenchModel model;
    model.applyArtifact(artifact("era5-a", 104.0, 24.0, 104.25, 24.25));
    expect(model.viewModel().activeArtifactId == "era5-a",
           "new artifact must open once");
    expect(model.hasArtifact("era5-a"), "artifact must be retained");

    std::string error;
    expect(model.dispatch(action(ScienceWorkbenchActionKind::CloseReport), error),
           "close must succeed");
    expect(model.viewModel().activeArtifactId.empty(),
           "close must hide active report");
    expect(model.hasArtifact("era5-a"), "close must not delete artifact");

    ScienceWorkbenchAction open = action(
        ScienceWorkbenchActionKind::OpenReport);
    open.artifactId = "era5-a";
    expect(model.dispatch(open, error), "retained report must reopen");
    expect(model.dispatch(action(ScienceWorkbenchActionKind::MinimizeReport),
                          error),
           "minimize must succeed");
    expect(model.hasArtifact("era5-a"), "minimize must not delete artifact");
    expect(model.viewModel().minimizedArtifactIds.size() == 1,
           "minimize must create one shelf entry");

    ScienceWorkbenchAction remove = action(
        ScienceWorkbenchActionKind::RemoveArtifact);
    remove.artifactId = "era5-a";
    expect(model.dispatch(remove, error), "explicit remove must succeed");
    expect(!model.hasArtifact("era5-a"),
           "explicit remove must delete model artifact reference");
}

void testFailureRetainsPreviousReadyArtifact()
{
    ScienceWorkbenchModel model;
    model.applyArtifact(artifact("ready-before-failure", 139.0, 35.0,
                                 139.25, 35.25));
    makeRunnable(model);
    std::string error;
    expect(model.dispatch(action(ScienceWorkbenchActionKind::Run), error),
           "second run must begin");

    earthscience::ScienceJobSnapshot snapshot;
    snapshot.state = earthscience::ScienceJobState::Failed;
    snapshot.message = "provider unavailable";
    model.applyJobSnapshot(snapshot);

    expect(model.viewModel().phase == ScienceWorkbenchPhase::Failed,
           "failed snapshot must set failed phase");
    expect(model.hasArtifact("ready-before-failure"),
           "failed run must retain older artifact");
    expect(model.viewModel().activeArtifactId == "ready-before-failure",
           "failed run must leave older report accessible");
}

void testReadyAnalysisOpensItsReportInsteadOfOldPreview()
{
    ScienceWorkbenchModel model;
    model.applyArtifact(artifact("old-preview", 100.0, 20.0, 101.0, 21.0));

    earthscience::ScienceJobSnapshot snapshot;
    snapshot.jobId = 12;
    snapshot.state = earthscience::ScienceJobState::Ready;
    snapshot.progress.stage = earthscience::ScienceProgressStage::Ready;
    snapshot.displayArtifact =
        artifact("old-preview", 100.0, 20.0, 101.0, 21.0);
    auto analysis = artifact("new-analysis", 104.0, 24.0, 105.0, 25.0);
    const_cast<earthscience::ScienceArtifact*>(analysis.get())->generation =
        snapshot.jobId;
    snapshot.lastSuccessfulAnalysisArtifact = analysis;

    model.applyJobSnapshot(snapshot);
    expect(model.viewModel().activeArtifactId == "new-analysis",
           "completed analysis must open its report instead of the retained"
           " map preview");
}
}

int main()
{
    testTargetLockIsImmutableAcrossCameraMotion();
    testRunRequiresValidFrozenDraftAndEmitsOnce();
    testProviderDraftConfigurationPreservesLockedGeometry();
    testRunAcceptsProviderValidatedIntervalAndInstantTime();
    testReportLifecycleDoesNotDeleteOnCloseOrMinimize();
    testFailureRetainsPreviousReadyArtifact();
    testReadyAnalysisOpensItsReportInsteadOfOldPreview();
    std::cout << "ScienceWorkbenchModel tests passed" << std::endl;
    return 0;
}
