#ifndef EARTH_SCIENCE_EARTH_PANEL_H
#define EARTH_SCIENCE_EARTH_PANEL_H

#include <ScienceQueryTypes.h>

#include <cstdint>
#include <string>
#include <vector>

class LayerManager;
class SciencePreviewLayer;

namespace earthscience { class ScienceQueryService; }
namespace osgVerse { class EarthManipulator; }

enum class SciencePanelMode
{
    Preview,
    PointSeries,
    RegionalChange,
};

enum class SciencePanelLocationMode
{
    CurrentLocation,
    CurrentViewFootprint,
};

enum class SciencePanelMetricChoice
{
    DirectionChange,
    VectorDisplacement,
    DirectionAndDisplacement,
};

enum class ScienceHelpTopic
{
    DataMeaning,
    PreviewColors,
    Sentinel2Meaning,
    Sentinel2NaturalColor,
    Sentinel2Cloud,
    Sentinel2Limits,
    CopernicusDemMeaning,
    CopernicusDemColors,
    CopernicusDemLimits,
    Era5Meaning,
    Era5Limits,
    Pca,
    Clusters,
    EmbeddingMetrics,
    Hotspots,
    ScientificLimits,
    Provenance,
};

struct SciencePanelModeCapabilities
{
    bool showsSingleYear = false;
    bool showsYearRange = false;
    bool showsYearPair = false;
    bool supportsGrid = false;
    bool supportsPca = false;
    bool supportsClustering = false;
};

struct SciencePanelState
{
    std::string sourceId = "alphaearth-foundations";
    SciencePanelMode mode = SciencePanelMode::Preview;
    SciencePanelLocationMode locationMode = SciencePanelLocationMode::CurrentLocation;
    int firstYear = 2017;
    int lastYear = 2025;
    int baselineYear = 2017;
    int comparisonYear = 2025;
    int gridSize = 128;
    SciencePanelMetricChoice pointMetricChoice =
        SciencePanelMetricChoice::DirectionAndDisplacement;
    earthscience::ScienceMetric regionalMetric =
        earthscience::ScienceMetric::CosineDistance;
    double hotspotQuantile = 0.90;
    bool advancedOpen = false;
    bool resultExpanded = false;
    bool enablePca = false;
    bool enableClustering = false;
    int clusterCount = 4;
    int sentinelWindowDays = 30;
    double sentinelMaximumCloudPercent = 40.0;
};

enum class SciencePanelResultKind
{
    Idle,
    Queued,
    Active,
    NoCoverage,
    Failed,
    Cancelled,
    Stale,
    Ready,
};

enum class SciencePanelSeverity
{
    Neutral,
    Info,
    Success,
    Warning,
    Error,
};

enum class SciencePanelDisplayKind
{
    NoArtifact,
    Publishing,
    LoadedVisible,
    LoadedHidden,
    AnalysisReadyWithoutRaster,
    FailureRetained,
    RendererUnavailable,
};

enum class SciencePanelWorkflowDecision
{
    Wait,
    SubmitAnalysis,
    Abort,
};

struct SciencePanelRetentionPresentation
{
    bool present = false;
    SciencePanelMode mode = SciencePanelMode::Preview;
    SciencePanelSeverity severity = SciencePanelSeverity::Neutral;
    std::string title;
    std::string reason;
};

struct SciencePanelPresentation
{
    SciencePanelResultKind kind = SciencePanelResultKind::Idle;
    SciencePanelSeverity severity = SciencePanelSeverity::Neutral;
    std::string title;
    std::string stageText;
    std::string sourceText;
    std::string retryText;
    std::string detail;
    std::string progressText;
    SciencePanelRetentionPresentation retention;
    bool busy = false;
    bool progressDeterminate = false;
};

struct SciencePanelWorkflowStrip
{
    std::string source;
    std::string area;
    std::string time;
    std::string method;
    std::string stage;
    std::string visibleArtifactId;
};

struct SciencePanelDisplayPresentation
{
    SciencePanelDisplayKind kind = SciencePanelDisplayKind::NoArtifact;
    SciencePanelSeverity severity = SciencePanelSeverity::Neutral;
    std::string title;
    std::string detail;
};

struct ScienceCostPresentation
{
    std::string sourceBytes;
    std::string residentMemory;
    std::string resultCells;
    std::string duration;
    bool requiresConfirmation = false;
};

struct ScienceArtifactUiPresentation
{
    bool matchesDraft = false;
    bool showPcaSummary = false;
    bool showClusterSummary = false;
    std::string scopeLabel;
    std::string pendingSettingsLabel;
};

SciencePanelPresentation describeScienceSnapshot(
    const earthscience::ScienceJobSnapshot& snapshot);
SciencePanelPresentation describeScienceSnapshot(
    const earthscience::ScienceJobSnapshot& snapshot,
    SciencePanelMode mode);
SciencePanelWorkflowStrip describeScienceWorkflowStrip(
    const earthscience::ScienceSourceDescriptor& source,
    SciencePanelMode mode,
    const SciencePanelState& state,
    const earthscience::GeoTemporalQuery& draft,
    const earthscience::ScienceJobSnapshot& snapshot,
    const std::string& visibleArtifactId);
SciencePanelDisplayPresentation describeScienceDisplayState(
    const earthscience::ScienceJobSnapshot& snapshot,
    SciencePanelMode mode,
    bool layerVisible,
    bool publisherHasArtifact,
    bool rendererUnavailable,
    const std::string& rendererMessage);
SciencePanelModeCapabilities sciencePanelModeCapabilities(
    SciencePanelMode mode);
SciencePanelModeCapabilities sciencePanelModeCapabilities(
    const earthscience::ScienceSourceDescriptor& source,
    SciencePanelMode mode);
const earthscience::ScienceSourceDescriptor* resolveSciencePanelSource(
    const std::vector<earthscience::ScienceSourceDescriptor>& sources,
    const std::string& sourceId);
std::vector<SciencePanelMode> sciencePanelModesForSource(
    const earthscience::ScienceSourceDescriptor& source);
SciencePanelMode activeSciencePanelMode(
    const earthscience::ScienceSourceDescriptor& source,
    SciencePanelMode requested);
bool sciencePanelRequiresContextPreview(
    const earthscience::ScienceSourceDescriptor& source,
    SciencePanelMode mode);
const char* sciencePanelPrimaryActionLabel(SciencePanelMode mode);
const char* sciencePanelPrimaryActionLabel(
    SciencePanelMode mode, const std::string& sourceId);
const char* sciencePanelModeLabel(
    SciencePanelMode mode, const std::string& sourceId);
const char* sciencePanelModeDescription(
    SciencePanelMode mode, const std::string& sourceId);
std::vector<earthscience::ScienceMetric> sciencePanelSelectedMetrics(
    SciencePanelMode mode, const SciencePanelState& state);
const char* sciencePanelMetricLabel(earthscience::ScienceMetric metric);
const char* sciencePanelMetricDescription(SciencePanelMetricChoice choice);
std::string sciencePanelSelectionSummary(
    SciencePanelMode mode, const SciencePanelState& state,
    const std::string& sourceId);
bool sciencePanelSnapshotMatchesDraft(
    const earthscience::ScienceJobSnapshot& snapshot,
    const earthscience::GeoTemporalQuery& currentDraft);
bool acknowledgeSciencePanelSubmission(
    std::uint64_t submittedJobId,
    const earthscience::ScienceJobSnapshot& submittedSnapshot,
    SciencePanelState* state);
ScienceArtifactUiPresentation describeScienceArtifactUi(
    const earthscience::ScienceArtifact& artifact,
    const earthscience::GeoTemporalQuery& currentDraft);
const char* scienceHelpTopicTitle(ScienceHelpTopic topic);
const char* scienceHelpTopicBody(ScienceHelpTopic topic);
ScienceCostPresentation describeScienceCost(
    const earthscience::ScienceQueryCost& cost);
std::shared_ptr<const earthscience::ScienceArtifact> selectSciencePanelArtifact(
    const earthscience::ScienceJobSnapshot& snapshot,
    SciencePanelMode mode);
bool sciencePanelEstimateRequiresConfirmation(
    const earthscience::GeoTemporalQuery& query,
    const earthscience::ScienceQueryCost& cost);
std::string sciencePanelEstimateBindingKey(
    const earthscience::GeoTemporalQuery& query,
    const earthscience::ScienceQueryCost& cost);
bool sciencePanelEstimateConfirmationMatches(
    const earthscience::GeoTemporalQuery& query,
    const earthscience::ScienceQueryCost& cost,
    const std::string& confirmedBindingKey);
std::vector<std::string> describeScienceArtifactEvidence(
    const earthscience::ScienceArtifact& artifact);
std::vector<std::string> describeScienceAnalysisHighlights(
    const earthscience::ScienceArtifact& artifact);
SciencePanelWorkflowDecision sciencePanelPendingAnalysisDecision(
    std::uint64_t contextPreviewJobId,
    const earthscience::ScienceJobSnapshot& snapshot);

class ScienceEarthPanel
{
public:
    void drawOperations(earthscience::ScienceQueryService* service,
                        SciencePreviewLayer* previewLayer,
                        LayerManager* layers,
                        osgVerse::EarthManipulator* manipulator);
    void drawResults(earthscience::ScienceQueryService* service,
                     SciencePreviewLayer* previewLayer,
                     LayerManager* layers);
    const SciencePanelState& state() const { return _state; }
    void setStateForTest(const SciencePanelState& state) { _state = state; }

private:
    SciencePanelState _state;
    earthscience::ScienceQueryCost _displayedCost;
    std::string _displayedEstimateKey;
    bool _estimateVisible = false;
    std::string _confirmedEstimateKey;
    earthscience::GeoTemporalQuery _currentDraft;
    bool _hasCurrentDraft = false;
    earthscience::GeoTemporalQuery _pendingAnalysisQuery;
    std::uint64_t _pendingContextJobId = 0;
    bool _hasPendingAnalysis = false;
    bool _workflowFailed = false;
    std::string _workflowMessage;
};

#endif
