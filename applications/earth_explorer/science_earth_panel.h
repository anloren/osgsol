#ifndef EARTH_SCIENCE_EARTH_PANEL_H
#define EARTH_SCIENCE_EARTH_PANEL_H

#include <ScienceQueryTypes.h>

#include <string>

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

struct SciencePanelState
{
    SciencePanelMode mode = SciencePanelMode::Preview;
    SciencePanelLocationMode locationMode = SciencePanelLocationMode::CurrentLocation;
    int firstYear = 2017;
    int lastYear = 2025;
    int baselineYear = 2017;
    int comparisonYear = 2025;
    int gridSize = 128;
    bool advancedOpen = false;
    bool resultExpanded = true;
    bool enablePca = false;
    bool enableClustering = false;
    int clusterCount = 4;
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

struct SciencePanelPresentation
{
    SciencePanelResultKind kind = SciencePanelResultKind::Idle;
    SciencePanelSeverity severity = SciencePanelSeverity::Neutral;
    std::string title;
    std::string stageText;
    std::string detail;
    std::string progressText;
    std::string retentionReason;
    bool busy = false;
    bool progressDeterminate = false;
};

struct ScienceCostPresentation
{
    std::string sourceBytes;
    std::string residentMemory;
    std::string resultCells;
    std::string duration;
    bool requiresConfirmation = false;
};

SciencePanelPresentation describeScienceSnapshot(
    const earthscience::ScienceJobSnapshot& snapshot);
ScienceCostPresentation describeScienceCost(
    const earthscience::ScienceQueryCost& cost);
std::shared_ptr<const earthscience::ScienceArtifact> selectSciencePanelArtifact(
    const earthscience::ScienceJobSnapshot& snapshot,
    SciencePanelMode mode);

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
    bool _estimateConfirmed = false;
};

#endif
