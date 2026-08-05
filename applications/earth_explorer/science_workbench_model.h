#ifndef EARTH_SCIENCE_WORKBENCH_MODEL_H
#define EARTH_SCIENCE_WORKBENCH_MODEL_H

#include "project/earth_temporal_controller.h"

#include <ScienceQueryTypes.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class ScienceWorkbenchPhase
{
    Draft,
    TargetLocked,
    ReadyToRun,
    Queued,
    Fetching,
    Analyzing,
    Ready,
    Failed,
    Cancelled,
};

struct ScienceTargetPresentation
{
    earthscience::ScienceGeometry requested;
    earthscience::ScienceGeometry actualCoverage;
    earthscience::ScienceWgs84Point requestedCenter;
    earthscience::ScienceWgs84Point returnedCenter;
    bool locked = false;
    bool hasActualCoverage = false;
};

struct ScienceWorkbenchViewModel
{
    std::uint64_t revision = 0;
    ScienceWorkbenchPhase phase = ScienceWorkbenchPhase::Draft;
    earthscience::GeoTemporalQuery draft;
    ScienceTargetPresentation target;
    earthscience::ScienceQueryCost cost;
    earthscience::ScienceProgress progress;
    std::string selectedAnalysisId;
    std::string selectedMethodId;
    std::string selectedMetricId;
    int selectedYear = 0;
    std::string activeArtifactId;
    std::vector<std::string> availableArtifactIds;
    std::vector<std::string> minimizedArtifactIds;
    std::string errorCode;
    std::string errorMessage;
    earthproject::EarthTemporalState temporal;
};

enum class ScienceWorkbenchActionKind
{
    SelectSource,
    SelectAnalysis,
    LockMapCenter,
    LockCurrentView,
    UpdateLockedTarget,
    SetYearRange,
    SetMethod,
    Run,
    Cancel,
    OpenReport,
    MinimizeReport,
    CloseReport,
    RemoveArtifact,
    FocusTarget,
    SelectMetric,
    SelectYear,
};

struct ScienceWorkbenchAction
{
    ScienceWorkbenchActionKind kind = ScienceWorkbenchActionKind::SelectSource;
    std::string sourceId;
    std::string analysisId;
    std::string methodId;
    std::string artifactId;
    std::string metricId;
    earthscience::ScienceGeometry geometry;
    int firstYear = 0;
    int lastYear = 0;
    int selectedYear = 0;
};

class ScienceWorkbenchModel
{
public:
    void configureTemporalSources(
        const std::vector<earthscience::ScienceSourceDescriptor>& sources);
    void updateLiveCameraContext(
        const earthscience::ScienceGeometry& mapCenterPoint,
        const earthscience::ScienceGeometry& visibleBounds);
    bool dispatch(const ScienceWorkbenchAction& action, std::string& error);
    const ScienceWorkbenchViewModel& viewModel() const { return _view; }
    std::optional<earthscience::GeoTemporalQuery> takePendingSubmission();
    void configureDraft(const earthscience::GeoTemporalQuery& draft);
    void applyCost(const earthscience::ScienceQueryCost& cost);
    void applyProgress(const earthscience::ScienceProgress& progress);
    void applyJobSnapshot(const earthscience::ScienceJobSnapshot& snapshot);
    void applyArtifact(
        std::shared_ptr<const earthscience::ScienceArtifact> artifact);
    bool hasArtifact(const std::string& artifactId) const;
    std::shared_ptr<const earthscience::ScienceArtifact> artifact(
        const std::string& artifactId) const;

private:
    bool fail(const std::string& code, std::string& error);
    void changed();
    void refreshDraftPhase();
    void lockTarget(const earthscience::ScienceGeometry& geometry);
    void removeMinimized(const std::string& artifactId);
    bool updateTemporalRequest(
        const earthscience::GeoTemporalQuery& query,
        std::string* errorCode = nullptr);
    void syncTemporalView();

    ScienceWorkbenchViewModel _view;
    earthscience::ScienceGeometry _liveMapCenter;
    earthscience::ScienceGeometry _liveVisibleBounds;
    bool _hasLiveMapCenter = false;
    bool _hasLiveVisibleBounds = false;
    std::optional<earthscience::GeoTemporalQuery> _pendingSubmission;
    std::unordered_map<std::string,
        std::shared_ptr<const earthscience::ScienceArtifact>> _artifacts;
    earthproject::EarthTemporalController _temporalController;
    earthproject::TemporalRequestToken _activeTemporalToken;
};

#endif
