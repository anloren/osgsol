#include "science_earth_panel.h"
#include "science_query_builder.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x "\n"; \
    return EXIT_FAILURE; } } while (0)

namespace
{
earthscience::ScienceJobSnapshot snapshot(
    earthscience::ScienceJobState state,
    earthscience::ScienceProgressStage stage,
    const std::string& message)
{
    earthscience::ScienceJobSnapshot value;
    value.state = state;
    value.progress.stage = stage;
    value.message = message;
    return value;
}

std::shared_ptr<const earthscience::ScienceArtifact> retainedArtifact()
{
    std::shared_ptr<earthscience::ScienceArtifact> artifact =
        std::make_shared<earthscience::ScienceArtifact>();
    artifact->artifactId = "retained-analysis";
    artifact->query.outputKind = earthscience::ScienceOutputKind::TimeSeries;
    artifact->analysis.kind = earthscience::ScienceAnalysisKind::PointSeries;
    return artifact;
}

std::shared_ptr<const earthscience::ScienceArtifact> artifact(
    const std::string& id,
    earthscience::ScienceOutputKind outputKind =
        earthscience::ScienceOutputKind::RasterLayer,
    earthscience::ScienceAnalysisKind analysisKind =
        earthscience::ScienceAnalysisKind::None)
{
    std::shared_ptr<earthscience::ScienceArtifact> value =
        std::make_shared<earthscience::ScienceArtifact>();
    value->artifactId = id;
    value->query.outputKind = outputKind;
    value->analysis.kind = analysisKind;
    return value;
}

std::string joined(const std::vector<std::string>& lines)
{
    std::string result;
    for (const std::string& line : lines)
    {
        if (!result.empty()) result += '\n';
        result += line;
    }
    return result;
}
}

int main()
{
    static_assert(!std::is_convertible<earthscience::ScienceProgress, float>::value,
                  "structured science progress must not silently become a slider value");

    ScienceEarthPanel panel;
    const SciencePanelState& defaults = panel.state();
    CHECK(defaults.mode == SciencePanelMode::Preview);
    CHECK(defaults.sourceId == "alphaearth-foundations");
    CHECK(defaults.sentinelWindowDays == 30);
    CHECK(defaults.sentinelMaximumCloudPercent == 40.0);
    CHECK(defaults.locationMode == SciencePanelLocationMode::CurrentLocation);
    CHECK(defaults.firstYear == 2017);
    CHECK(defaults.lastYear == 2025);
    CHECK(defaults.baselineYear == 2017);
    CHECK(defaults.comparisonYear == 2025);
    CHECK(defaults.gridSize == 128);
    CHECK(!defaults.advancedOpen);
    CHECK(!defaults.resultExpanded);
    CHECK(!defaults.enablePca);
    CHECK(!defaults.enableClustering);

    earthscience::ScienceSourceDescriptor alphaSource;
    alphaSource.id = "alphaearth-foundations";
    alphaSource.name = "AlphaEarth Foundations";
    alphaSource.firstYear = 2017;
    alphaSource.lastYear = 2025;
    alphaSource.nativeResolutionMeters = 10.0;
    alphaSource.capabilities.pointQuery = true;
    alphaSource.capabilities.explicitYears = true;
    alphaSource.capabilities.rasterLayerOutput = true;
    alphaSource.capabilities.timeSeriesOutput = true;
    alphaSource.capabilities.analysisOutput = true;
    alphaSource.capabilities.minimumSpanMeters = 2560.0;
    alphaSource.capabilities.maximumSpanMeters = 81920.0;
    earthscience::ScienceVisualizationDescriptor alphaVisualization;
    alphaVisualization.id = "false-color-a01-a16-a09";
    alphaVisualization.channelVariables = {"A01", "A16", "A09"};
    alphaSource.visualizations.push_back(alphaVisualization);

    earthscience::ScienceSourceDescriptor sentinelSource;
    sentinelSource.id = "sentinel-2-l2a";
    sentinelSource.name = "Sentinel-2 Level-2A";
    sentinelSource.nativeResolutionMeters = 10.0;
    sentinelSource.capabilities.pointQuery = true;
    sentinelSource.capabilities.intervalTime = true;
    sentinelSource.capabilities.rasterLayerOutput = true;
    sentinelSource.capabilities.minimumSpanMeters = 2560.0;
    sentinelSource.capabilities.maximumSpanMeters = 81920.0;
    earthscience::ScienceVisualizationDescriptor sentinelVisualization;
    sentinelVisualization.id = "natural-color-visual";
    sentinelVisualization.channelVariables = {"visual"};
    sentinelSource.visualizations.push_back(sentinelVisualization);

    const std::vector<earthscience::ScienceSourceDescriptor> reorderedSources =
        {sentinelSource, alphaSource};
    CHECK(resolveSciencePanelSource(
              reorderedSources, "alphaearth-foundations")->id ==
          "alphaearth-foundations");
    CHECK(resolveSciencePanelSource(reorderedSources, "missing")->id ==
          "alphaearth-foundations");
    const std::vector<SciencePanelMode> alphaModes =
        sciencePanelModesForSource(alphaSource);
    const std::vector<SciencePanelMode> sentinelModes =
        sciencePanelModesForSource(sentinelSource);
    CHECK(alphaModes.size() == 3);
    CHECK(sentinelModes ==
          std::vector<SciencePanelMode>({SciencePanelMode::Preview}));
    CHECK(activeSciencePanelMode(
              sentinelSource, SciencePanelMode::RegionalChange) ==
          SciencePanelMode::Preview);
    CHECK(activeSciencePanelMode(
              alphaSource, SciencePanelMode::RegionalChange) ==
          SciencePanelMode::RegionalChange);
    CHECK(std::string(sciencePanelPrimaryActionLabel(
              SciencePanelMode::Preview, sentinelSource.id)) ==
          u8"加载 Sentinel-2 真彩场景");

    const earthscience::GeoTemporalQuery sentinel7 =
        makeSentinel2PreviewQuery(
            sentinelSource, 35.68, 139.76,
            "2026-07-18T23:59:59Z", 7, 10.0, 10000.0);
    CHECK(sentinel7.sourceId == "sentinel-2-l2a");
    CHECK(sentinel7.time.mode == earthscience::ScienceTimeMode::Interval);
    CHECK(sentinel7.time.intervalStart == "2026-07-11T23:59:59Z");
    CHECK(sentinel7.time.intervalEnd == "2026-07-18T23:59:59Z");
    CHECK(sentinel7.sceneFilters.maximumCloudCoverPercent == 10.0);
    CHECK(sentinel7.sceneFilters.maximumScenes == 10);
    CHECK(sentinel7.variables == std::vector<std::string>({"visual"}));
    CHECK(sentinel7.visualizationId == "natural-color-visual");
    const earthscience::GeoTemporalQuery sentinel30 =
        makeSentinel2PreviewQuery(
            sentinelSource, 35.68, 139.76,
            "2026-07-18T23:59:59Z", 30, 20.0, 10000.0);
    CHECK(sentinel30.time.intervalStart == "2026-06-18T23:59:59Z");
    const earthscience::GeoTemporalQuery sentinel90 =
        makeSentinel2PreviewQuery(
            sentinelSource, 35.68, 139.76,
            "2026-07-18T23:59:59Z", 90, 100.0, 10000.0);
    CHECK(sentinel90.time.intervalStart == "2026-04-19T23:59:59Z");

    const SciencePanelModeCapabilities previewCapabilities =
        sciencePanelModeCapabilities(SciencePanelMode::Preview);
    CHECK(previewCapabilities.showsSingleYear);
    CHECK(!previewCapabilities.showsYearRange);
    CHECK(!previewCapabilities.showsYearPair);
    CHECK(!previewCapabilities.supportsGrid);
    CHECK(!previewCapabilities.supportsPca);
    CHECK(!previewCapabilities.supportsClustering);
    CHECK(std::string(sciencePanelPrimaryActionLabel(
              SciencePanelMode::Preview)) == u8"加载伪彩预览");

    const SciencePanelModeCapabilities seriesCapabilities =
        sciencePanelModeCapabilities(SciencePanelMode::PointSeries);
    CHECK(!seriesCapabilities.showsSingleYear);
    CHECK(seriesCapabilities.showsYearRange);
    CHECK(!seriesCapabilities.showsYearPair);
    CHECK(!seriesCapabilities.supportsGrid);
    CHECK(!seriesCapabilities.supportsPca);
    CHECK(!seriesCapabilities.supportsClustering);
    CHECK(std::string(sciencePanelPrimaryActionLabel(
              SciencePanelMode::PointSeries)) == u8"分析点位年度变化");

    const SciencePanelModeCapabilities regionalCapabilities =
        sciencePanelModeCapabilities(SciencePanelMode::RegionalChange);
    CHECK(!regionalCapabilities.showsSingleYear);
    CHECK(!regionalCapabilities.showsYearRange);
    CHECK(regionalCapabilities.showsYearPair);
    CHECK(regionalCapabilities.supportsGrid);
    CHECK(regionalCapabilities.supportsPca);
    CHECK(regionalCapabilities.supportsClustering);
    CHECK(std::string(sciencePanelPrimaryActionLabel(
              SciencePanelMode::RegionalChange)) == u8"分析当前视野变化");

    const SciencePanelPresentation queued = describeScienceSnapshot(snapshot(
        earthscience::ScienceJobState::Queued,
        earthscience::ScienceProgressStage::Queued, "waiting for provider"));
    CHECK(queued.kind == SciencePanelResultKind::Queued);
    CHECK(queued.severity == SciencePanelSeverity::Info);
    CHECK(!queued.title.empty());
    CHECK(!queued.stageText.empty());

    earthscience::ScienceJobSnapshot locating = snapshot(
        earthscience::ScienceJobState::Fetching,
        earthscience::ScienceProgressStage::Locating, "locating annual tile");
    const SciencePanelPresentation locatingView = describeScienceSnapshot(locating);
    CHECK(locatingView.kind == SciencePanelResultKind::Active);
    CHECK(!locatingView.progressDeterminate);
    CHECK(locatingView.progressText.find('%') == std::string::npos);

    earthscience::ScienceJobSnapshot reading = snapshot(
        earthscience::ScienceJobState::Fetching,
        earthscience::ScienceProgressStage::Reading, "reading embedding bands");
    reading.progress.determinate = true;
    reading.progress.completedUnits = 32;
    reading.progress.totalUnits = 64;
    reading.progress.unit = "components";
    const SciencePanelPresentation readingView = describeScienceSnapshot(reading);
    CHECK(readingView.kind == SciencePanelResultKind::Active);
    CHECK(readingView.progressDeterminate);
    CHECK(readingView.progressText.find("32 / 64") != std::string::npos);

    const SciencePanelPresentation noCoverage = describeScienceSnapshot(snapshot(
        earthscience::ScienceJobState::Failed,
        earthscience::ScienceProgressStage::Failed,
        "No AlphaEarth tile covers this point and year"));
    const SciencePanelPresentation failed = describeScienceSnapshot(snapshot(
        earthscience::ScienceJobState::Failed,
        earthscience::ScienceProgressStage::Failed, "provider read failed"));
    const SciencePanelPresentation cancelled = describeScienceSnapshot(snapshot(
        earthscience::ScienceJobState::Cancelled,
        earthscience::ScienceProgressStage::Cancelled, "cancelled by user"));
    const SciencePanelPresentation ready = describeScienceSnapshot(snapshot(
        earthscience::ScienceJobState::Ready,
        earthscience::ScienceProgressStage::Ready, "analysis ready"));
    const SciencePanelPresentation stale = describeScienceSnapshot(snapshot(
        earthscience::ScienceJobState::Cancelled,
        earthscience::ScienceProgressStage::Cancelled,
        "stale generation discarded after a newer request"));
    CHECK(noCoverage.kind == SciencePanelResultKind::NoCoverage);
    CHECK(failed.kind == SciencePanelResultKind::Failed);
    CHECK(cancelled.kind == SciencePanelResultKind::Cancelled);
    CHECK(ready.kind == SciencePanelResultKind::Ready);
    CHECK(stale.kind == SciencePanelResultKind::Stale);
    CHECK(noCoverage.title != failed.title);
    CHECK(failed.title != cancelled.title);
    CHECK(cancelled.title != ready.title);
    CHECK(stale.title != cancelled.title);
    CHECK(failed.severity == SciencePanelSeverity::Error);
    CHECK(noCoverage.severity == SciencePanelSeverity::Warning);

    earthscience::ScienceJobSnapshot replacementFailure = snapshot(
        earthscience::ScienceJobState::Failed,
        earthscience::ScienceProgressStage::Failed, "network timeout");
    replacementFailure.lastSuccessfulAnalysisArtifact = retainedArtifact();
    const SciencePanelPresentation retained =
        describeScienceSnapshot(replacementFailure,
                                SciencePanelMode::PointSeries);
    CHECK(retained.kind == SciencePanelResultKind::Failed);
    CHECK(retained.severity == SciencePanelSeverity::Error);
    CHECK(retained.title.find("Request failed") != std::string::npos);
    CHECK(retained.detail.find("network timeout") != std::string::npos);
    CHECK(retained.retention.present);
    CHECK(!retained.retention.reason.empty());
    CHECK(retained.retention.reason.find("retained-analysis") !=
          std::string::npos);
    CHECK(retained.detail != retained.retention.reason);

    earthscience::ScienceJobSnapshot independentResults;
    independentResults.lastSuccessfulPreviewArtifact = artifact("preview-new");
    independentResults.lastSuccessfulAnalysisArtifact = artifact(
        "analysis-old", earthscience::ScienceOutputKind::TimeSeries,
        earthscience::ScienceAnalysisKind::PointSeries);
    independentResults.displayArtifact =
        independentResults.lastSuccessfulPreviewArtifact;
    CHECK(selectSciencePanelArtifact(
        independentResults, SciencePanelMode::Preview)->artifactId ==
        "preview-new");
    CHECK(selectSciencePanelArtifact(
        independentResults, SciencePanelMode::PointSeries)->artifactId ==
        "analysis-old");

    earthscience::ScienceJobSnapshot readyPreviewJob = snapshot(
        earthscience::ScienceJobState::Ready,
        earthscience::ScienceProgressStage::Ready, "preview ready");
    readyPreviewJob.jobId = 42;
    readyPreviewJob.query.outputKind =
        earthscience::ScienceOutputKind::RasterLayer;
    const SciencePanelPresentation unrelatedRegionalStatus =
        describeScienceSnapshot(
            readyPreviewJob, SciencePanelMode::RegionalChange);
    CHECK(unrelatedRegionalStatus.kind == SciencePanelResultKind::Idle);
    CHECK(unrelatedRegionalStatus.title.find(u8"此模式尚无结果") !=
          std::string::npos);
    CHECK(unrelatedRegionalStatus.title.find("ready") == std::string::npos);

    earthscience::ScienceJobSnapshot runningPreviewJob = snapshot(
        earthscience::ScienceJobState::Fetching,
        earthscience::ScienceProgressStage::Reading, "reading preview tile");
    runningPreviewJob.jobId = 43;
    runningPreviewJob.query.outputKind =
        earthscience::ScienceOutputKind::RasterLayer;
    const SciencePanelPresentation runningFromAnotherMode =
        describeScienceSnapshot(
            runningPreviewJob, SciencePanelMode::RegionalChange);
    CHECK(runningFromAnotherMode.busy);
    CHECK(runningFromAnotherMode.kind == SciencePanelResultKind::Active);

    earthscience::ScienceArtifact regionalPcaArtifact;
    regionalPcaArtifact.query.outputKind =
        earthscience::ScienceOutputKind::Analysis;
    regionalPcaArtifact.query.analysis.kind =
        earthscience::ScienceAnalysisKind::RegionalChange;
    regionalPcaArtifact.query.analysis.gridSize = 128;
    regionalPcaArtifact.query.analysis.enablePca = true;
    regionalPcaArtifact.query.time.explicitYears = {2017, 2025};
    earthscience::GeoTemporalQuery matchingRegionalDraft =
        regionalPcaArtifact.query;
    ScienceArtifactUiPresentation artifactUi =
        describeScienceArtifactUi(regionalPcaArtifact, matchingRegionalDraft);
    CHECK(artifactUi.matchesDraft);
    CHECK(!artifactUi.showPcaSummary);
    CHECK(!artifactUi.showClusterSummary);
    CHECK(artifactUi.scopeLabel.find("2017") != std::string::npos);
    CHECK(artifactUi.scopeLabel.find("2025") != std::string::npos);
    CHECK(artifactUi.scopeLabel.find("128") != std::string::npos);
    CHECK(artifactUi.pendingSettingsLabel.empty());

    regionalPcaArtifact.analysis.pca.componentCount = 3;
    artifactUi = describeScienceArtifactUi(
        regionalPcaArtifact, matchingRegionalDraft);
    CHECK(artifactUi.showPcaSummary);
    CHECK(artifactUi.scopeLabel.find("PCA") != std::string::npos);
    matchingRegionalDraft.time.explicitYears = {2018, 2024};
    matchingRegionalDraft.analysis.baselineYear = 2018;
    matchingRegionalDraft.analysis.comparisonYear = 2024;
    artifactUi = describeScienceArtifactUi(
        regionalPcaArtifact, matchingRegionalDraft);
    CHECK(!artifactUi.matchesDraft);
    CHECK(!artifactUi.pendingSettingsLabel.empty());
    CHECK(artifactUi.scopeLabel.find("2017") != std::string::npos);
    CHECK(artifactUi.scopeLabel.find("2025") != std::string::npos);
    CHECK(artifactUi.scopeLabel.find("2018") == std::string::npos);
    CHECK(artifactUi.scopeLabel.find("2024") == std::string::npos);

    earthscience::ScienceArtifact pointSeriesArtifact;
    pointSeriesArtifact.query.outputKind =
        earthscience::ScienceOutputKind::TimeSeries;
    pointSeriesArtifact.query.analysis.kind =
        earthscience::ScienceAnalysisKind::PointSeries;
    pointSeriesArtifact.query.time.explicitYears = {
        2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024, 2025};
    const ScienceArtifactUiPresentation compactPointScope =
        describeScienceArtifactUi(
            pointSeriesArtifact, pointSeriesArtifact.query);
    CHECK(compactPointScope.scopeLabel.find("2017") != std::string::npos);
    CHECK(compactPointScope.scopeLabel.find("2025") != std::string::npos);
    CHECK(compactPointScope.scopeLabel.find("2018") == std::string::npos);

    earthscience::ScienceArtifact sentinelArtifact;
    sentinelArtifact.query = sentinel30;
    sentinelArtifact.raster.bounds = {139.74, 35.66, 139.78, 35.70};
    sentinelArtifact.raster.sourceResolutionMeters = 10.0;
    sentinelArtifact.raster.displayResolutionMeters = 10.0;
    earthscience::ScienceSourceReference sentinelReference;
    sentinelReference.datasetId = "S2C_54SUE_20260710_0_L2A";
    sentinelReference.acquisitionTime = "2026-07-10T01:37:22.464000Z";
    sentinelReference.actualCoverage = sentinelArtifact.raster.bounds;
    sentinelReference.fields = {
        {"scene_id", "Scene ID", sentinelReference.datasetId, ""},
        {"scene_cloud_cover", "Scene cloud cover", "11.17", "%"},
    };
    sentinelArtifact.sourceReferences.push_back(sentinelReference);
    ScienceArtifactUiPresentation sentinelUi = describeScienceArtifactUi(
        sentinelArtifact, sentinel30);
    CHECK(sentinelUi.matchesDraft);
    CHECK(sentinelUi.scopeLabel.find("Sentinel-2") != std::string::npos);
    CHECK(sentinelUi.scopeLabel.find(u8"真彩") != std::string::npos);
    CHECK(sentinelUi.scopeLabel.find("2026-07-10") != std::string::npos);
    CHECK(sentinelUi.scopeLabel.find(u8"伪彩") == std::string::npos);
    sentinelUi = describeScienceArtifactUi(sentinelArtifact, sentinel7);
    CHECK(!sentinelUi.matchesDraft);
    CHECK(!sentinelUi.pendingSettingsLabel.empty());
    const std::string sentinelEvidence = joined(
        describeScienceArtifactEvidence(sentinelArtifact));
    CHECK(sentinelEvidence.find("2026-06-18T23:59:59Z") !=
          std::string::npos);
    CHECK(sentinelEvidence.find("2026-07-10T01:37:22.464000Z") !=
          std::string::npos);
    CHECK(sentinelEvidence.find("S2C_54SUE_20260710_0_L2A") !=
          std::string::npos);
    CHECK(sentinelEvidence.find("11.17 %") != std::string::npos);

    const ScienceHelpTopic helpTopics[] = {
        ScienceHelpTopic::DataMeaning,
        ScienceHelpTopic::PreviewColors,
        ScienceHelpTopic::Sentinel2Meaning,
        ScienceHelpTopic::Sentinel2NaturalColor,
        ScienceHelpTopic::Sentinel2Cloud,
        ScienceHelpTopic::Sentinel2Limits,
        ScienceHelpTopic::Pca,
        ScienceHelpTopic::Clusters,
        ScienceHelpTopic::ScientificLimits,
        ScienceHelpTopic::Provenance,
    };
    for (ScienceHelpTopic topic : helpTopics)
    {
        CHECK(std::string(scienceHelpTopicTitle(topic)).size() > 2);
        CHECK(std::string(scienceHelpTopicBody(topic)).size() > 12);
    }
    CHECK(std::string(scienceHelpTopicBody(
              ScienceHelpTopic::Pca)).find(u8"局部数学") !=
          std::string::npos);
    CHECK(std::string(scienceHelpTopicBody(
              ScienceHelpTopic::PreviewColors)).find(u8"不是自然色") !=
          std::string::npos);
    CHECK(std::string(scienceHelpTopicBody(
              ScienceHelpTopic::ScientificLimits)).find(u8"不能单独证明") !=
          std::string::npos);
    CHECK(std::string(scienceHelpTopicBody(
              ScienceHelpTopic::Sentinel2NaturalColor)).find(
                  u8"不是原始反射率") != std::string::npos);
    CHECK(std::string(scienceHelpTopicBody(
              ScienceHelpTopic::Sentinel2Cloud)).find(
                  u8"不是当前") != std::string::npos);
    CHECK(std::string(scienceHelpTopicBody(
              ScienceHelpTopic::Sentinel2Limits)).find(
                  u8"不做") != std::string::npos);

    const SciencePanelPresentation sentinelNoScene = describeScienceSnapshot(
        snapshot(earthscience::ScienceJobState::Failed,
                 earthscience::ScienceProgressStage::Failed,
                 "No Sentinel-2 scene satisfies the cloud threshold"));
    CHECK(sentinelNoScene.kind == SciencePanelResultKind::NoCoverage);
    CHECK(sentinelNoScene.severity == SciencePanelSeverity::Warning);

    earthscience::ScienceJobSnapshot previewReplacementFailure = snapshot(
        earthscience::ScienceJobState::Failed,
        earthscience::ScienceProgressStage::Failed, "preview network timeout");
    previewReplacementFailure.query.outputKind =
        earthscience::ScienceOutputKind::RasterLayer;
    previewReplacementFailure.lastSuccessfulPreviewArtifact =
        artifact("preview-2022");
    previewReplacementFailure.lastSuccessfulAnalysisArtifact = artifact(
        "point-series-2017-2025",
        earthscience::ScienceOutputKind::TimeSeries,
        earthscience::ScienceAnalysisKind::PointSeries);
    CHECK(selectSciencePanelArtifact(
        previewReplacementFailure, SciencePanelMode::Preview)->artifactId ==
        "preview-2022");
    const SciencePanelPresentation retainedPreview = describeScienceSnapshot(
        previewReplacementFailure, SciencePanelMode::Preview);
    CHECK(retainedPreview.kind == SciencePanelResultKind::Failed);
    CHECK(retainedPreview.severity == SciencePanelSeverity::Error);
    CHECK(retainedPreview.title.find("Request failed") != std::string::npos);
    CHECK(retainedPreview.retention.reason.find("preview-2022") !=
          std::string::npos);
    CHECK(retainedPreview.retention.reason.find("point-series") ==
          std::string::npos);

    earthscience::ScienceJobSnapshot previewOnly;
    previewOnly.lastSuccessfulPreviewArtifact = artifact("preview-only");
    CHECK(!selectSciencePanelArtifact(
        previewOnly, SciencePanelMode::PointSeries));

    earthscience::ScienceJobSnapshot pointFailure = snapshot(
        earthscience::ScienceJobState::Failed,
        earthscience::ScienceProgressStage::Failed, "point analysis failed");
    pointFailure.query.outputKind = earthscience::ScienceOutputKind::TimeSeries;
    pointFailure.lastSuccessfulAnalysisArtifact = artifact(
        "point-retained", earthscience::ScienceOutputKind::TimeSeries,
        earthscience::ScienceAnalysisKind::PointSeries);
    CHECK(selectSciencePanelArtifact(
        pointFailure, SciencePanelMode::PointSeries)->artifactId ==
        "point-retained");
    CHECK(!selectSciencePanelArtifact(
        pointFailure, SciencePanelMode::RegionalChange));
    const SciencePanelPresentation retainedPoint = describeScienceSnapshot(
        pointFailure, SciencePanelMode::PointSeries);
    CHECK(retainedPoint.kind == SciencePanelResultKind::Failed);
    CHECK(retainedPoint.severity == SciencePanelSeverity::Error);
    CHECK(retainedPoint.title.find("Request failed") != std::string::npos);
    CHECK(retainedPoint.retention.reason.find("point-retained") !=
          std::string::npos);

    earthscience::ScienceJobSnapshot regionalFailure = snapshot(
        earthscience::ScienceJobState::Failed,
        earthscience::ScienceProgressStage::Failed, "regional analysis failed");
    regionalFailure.query.outputKind = earthscience::ScienceOutputKind::Analysis;
    regionalFailure.lastSuccessfulAnalysisArtifact = artifact(
        "regional-retained", earthscience::ScienceOutputKind::Analysis,
        earthscience::ScienceAnalysisKind::RegionalChange);
    CHECK(selectSciencePanelArtifact(
        regionalFailure, SciencePanelMode::RegionalChange)->artifactId ==
        "regional-retained");
    CHECK(!selectSciencePanelArtifact(
        regionalFailure, SciencePanelMode::PointSeries));
    const SciencePanelPresentation retainedRegional = describeScienceSnapshot(
        regionalFailure, SciencePanelMode::RegionalChange);
    CHECK(retainedRegional.kind == SciencePanelResultKind::Failed);
    CHECK(retainedRegional.severity == SciencePanelSeverity::Error);
    CHECK(retainedRegional.title.find("Request failed") != std::string::npos);
    CHECK(retainedRegional.retention.reason.find("regional-retained") !=
          std::string::npos);

    earthscience::ScienceJobSnapshot noCoverageRetained = snapshot(
        earthscience::ScienceJobState::Failed,
        earthscience::ScienceProgressStage::Failed,
        "No AlphaEarth tile covers this point and year");
    noCoverageRetained.lastSuccessfulPreviewArtifact =
        artifact("preview-no-coverage-retained");
    const SciencePanelPresentation noCoverageRetainedView =
        describeScienceSnapshot(noCoverageRetained, SciencePanelMode::Preview);
    CHECK(noCoverageRetainedView.kind == SciencePanelResultKind::NoCoverage);
    CHECK(noCoverageRetainedView.severity == SciencePanelSeverity::Warning);
    CHECK(noCoverageRetainedView.title.find("No coverage") !=
          std::string::npos);
    CHECK(noCoverageRetainedView.stageText.find("Failed") !=
          std::string::npos);
    CHECK(noCoverageRetainedView.detail == noCoverageRetained.message);
    CHECK(noCoverageRetainedView.retention.present);
    CHECK(noCoverageRetainedView.retention.mode == SciencePanelMode::Preview);
    CHECK(noCoverageRetainedView.retention.severity ==
          SciencePanelSeverity::Warning);
    CHECK(noCoverageRetainedView.retention.title.find("Preview") !=
          std::string::npos);
    CHECK(noCoverageRetainedView.retention.reason.find("No coverage") !=
          std::string::npos);
    CHECK(noCoverageRetainedView.retention.reason.find(
              "preview-no-coverage-retained") != std::string::npos);

    earthscience::ScienceJobSnapshot providerFailureRetained = snapshot(
        earthscience::ScienceJobState::Failed,
        earthscience::ScienceProgressStage::Failed,
        "provider read failed: checksum mismatch");
    providerFailureRetained.lastSuccessfulPreviewArtifact =
        artifact("preview-provider-failure-retained");
    const SciencePanelPresentation providerFailureRetainedView =
        describeScienceSnapshot(
            providerFailureRetained, SciencePanelMode::Preview);
    CHECK(providerFailureRetainedView.kind == SciencePanelResultKind::Failed);
    CHECK(providerFailureRetainedView.severity == SciencePanelSeverity::Error);
    CHECK(providerFailureRetainedView.title.find("Request failed") !=
          std::string::npos);
    CHECK(providerFailureRetainedView.stageText.find("Failed") !=
          std::string::npos);
    CHECK(providerFailureRetainedView.detail == providerFailureRetained.message);
    CHECK(providerFailureRetainedView.retention.present);
    CHECK(providerFailureRetainedView.retention.mode == SciencePanelMode::Preview);
    CHECK(providerFailureRetainedView.retention.severity ==
          SciencePanelSeverity::Warning);
    CHECK(providerFailureRetainedView.retention.title.find("Preview") !=
          std::string::npos);
    CHECK(providerFailureRetainedView.retention.reason.find(
              "Provider failure") != std::string::npos);
    CHECK(providerFailureRetainedView.retention.reason.find(
              "preview-provider-failure-retained") != std::string::npos);

    earthscience::ScienceJobSnapshot cancelledRetained = snapshot(
        earthscience::ScienceJobState::Cancelled,
        earthscience::ScienceProgressStage::Cancelled, "cancelled by user");
    cancelledRetained.lastSuccessfulAnalysisArtifact = artifact(
        "point-cancelled-retained", earthscience::ScienceOutputKind::TimeSeries,
        earthscience::ScienceAnalysisKind::PointSeries);
    const SciencePanelPresentation cancelledRetainedView =
        describeScienceSnapshot(cancelledRetained,
                                SciencePanelMode::PointSeries);
    CHECK(cancelledRetainedView.kind == SciencePanelResultKind::Cancelled);
    CHECK(cancelledRetainedView.severity == SciencePanelSeverity::Warning);
    CHECK(cancelledRetainedView.title.find("Request cancelled") !=
          std::string::npos);
    CHECK(cancelledRetainedView.stageText.find("Cancelled") !=
          std::string::npos);
    CHECK(cancelledRetainedView.detail == cancelledRetained.message);
    CHECK(cancelledRetainedView.retention.present);
    CHECK(cancelledRetainedView.retention.mode ==
          SciencePanelMode::PointSeries);
    CHECK(cancelledRetainedView.retention.severity ==
          SciencePanelSeverity::Warning);
    CHECK(cancelledRetainedView.retention.title.find("Point series") !=
          std::string::npos);
    CHECK(cancelledRetainedView.retention.reason.find("Cancelled") !=
          std::string::npos);
    CHECK(cancelledRetainedView.retention.reason.find(
              "point-cancelled-retained") != std::string::npos);

    earthscience::ScienceJobSnapshot staleRetained = snapshot(
        earthscience::ScienceJobState::Cancelled,
        earthscience::ScienceProgressStage::Cancelled,
        "stale generation discarded after a newer request");
    staleRetained.lastSuccessfulAnalysisArtifact = artifact(
        "regional-stale-retained", earthscience::ScienceOutputKind::Analysis,
        earthscience::ScienceAnalysisKind::RegionalChange);
    const SciencePanelPresentation staleRetainedView =
        describeScienceSnapshot(staleRetained,
                                SciencePanelMode::RegionalChange);
    CHECK(staleRetainedView.kind == SciencePanelResultKind::Stale);
    CHECK(staleRetainedView.severity == SciencePanelSeverity::Warning);
    CHECK(staleRetainedView.title.find("Stale result discarded") !=
          std::string::npos);
    CHECK(staleRetainedView.stageText.find("Cancelled") != std::string::npos);
    CHECK(staleRetainedView.detail == staleRetained.message);
    CHECK(staleRetainedView.retention.present);
    CHECK(staleRetainedView.retention.mode ==
          SciencePanelMode::RegionalChange);
    CHECK(staleRetainedView.retention.severity == SciencePanelSeverity::Warning);
    CHECK(staleRetainedView.retention.title.find("Regional change") !=
          std::string::npos);
    CHECK(staleRetainedView.retention.reason.find("Stale") !=
          std::string::npos);
    CHECK(staleRetainedView.retention.reason.find("regional-stale-retained") !=
          std::string::npos);

    CHECK(noCoverageRetainedView.title != providerFailureRetainedView.title);
    CHECK(providerFailureRetainedView.title != cancelledRetainedView.title);
    CHECK(cancelledRetainedView.title != staleRetainedView.title);
    CHECK(noCoverageRetainedView.retention.reason !=
          providerFailureRetainedView.retention.reason);
    CHECK(providerFailureRetainedView.retention.reason !=
          cancelledRetainedView.retention.reason);
    CHECK(cancelledRetainedView.retention.reason !=
          staleRetainedView.retention.reason);

    earthscience::ScienceQueryCost unknownDuration;
    unknownDuration.sourceBytesUpperBound = 4096;
    unknownDuration.residentBytesUpperBound = 8192;
    unknownDuration.resultCells = 65536;
    unknownDuration.requiresConfirmation = true;
    const ScienceCostPresentation cost = describeScienceCost(unknownDuration);
    CHECK(cost.requiresConfirmation);
    CHECK(cost.sourceBytes.find("4.0 KiB") != std::string::npos);
    CHECK(cost.residentMemory.find("8.0 KiB") != std::string::npos);
    CHECK(cost.resultCells.find("65,536") != std::string::npos);
    CHECK(cost.duration == u8"尚无可测时长 / duration not yet measurable");

    earthscience::ScienceQueryCost ordinaryCost;
    ordinaryCost.sourceBytesUpperBound = 9 * 4096;
    ordinaryCost.residentBytesUpperBound = 9 * 8192;
    ordinaryCost.resultCells = 9;
    earthscience::GeoTemporalQuery multiYear;
    multiYear.outputKind = earthscience::ScienceOutputKind::TimeSeries;
    multiYear.analysis.kind = earthscience::ScienceAnalysisKind::PointSeries;
    multiYear.time.explicitYears = {
        2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024, 2025};
    CHECK(!ordinaryCost.requiresConfirmation);
    CHECK(sciencePanelEstimateRequiresConfirmation(multiYear, ordinaryCost));
    const std::string multiYearBinding = sciencePanelEstimateBindingKey(
        multiYear, ordinaryCost);
    CHECK(!multiYearBinding.empty());
    CHECK(!sciencePanelEstimateConfirmationMatches(
        multiYear, ordinaryCost, std::string()));
    CHECK(sciencePanelEstimateConfirmationMatches(
        multiYear, ordinaryCost, multiYearBinding));

    earthscience::GeoTemporalQuery changedYears = multiYear;
    changedYears.time.explicitYears.pop_back();
    CHECK(!sciencePanelEstimateConfirmationMatches(
        changedYears, ordinaryCost, multiYearBinding));
    CHECK(sciencePanelEstimateBindingKey(changedYears, ordinaryCost) !=
          multiYearBinding);

    earthscience::GeoTemporalQuery singleYear = multiYear;
    singleYear.time.explicitYears = {2025};
    CHECK(!sciencePanelEstimateRequiresConfirmation(singleYear, ordinaryCost));
    earthscience::GeoTemporalQuery preview = singleYear;
    preview.outputKind = earthscience::ScienceOutputKind::RasterLayer;
    preview.analysis.kind = earthscience::ScienceAnalysisKind::None;
    CHECK(!sciencePanelEstimateRequiresConfirmation(preview, ordinaryCost));
    earthscience::GeoTemporalQuery regional256;
    regional256.outputKind = earthscience::ScienceOutputKind::Analysis;
    regional256.analysis.kind =
        earthscience::ScienceAnalysisKind::RegionalChange;
    regional256.analysis.gridSize = 256;
    regional256.time.explicitYears = {2017, 2025};
    CHECK(sciencePanelEstimateRequiresConfirmation(regional256, ordinaryCost));

    std::shared_ptr<earthscience::ScienceArtifact> previewEvidence =
        std::make_shared<earthscience::ScienceArtifact>();
    previewEvidence->query.outputKind =
        earthscience::ScienceOutputKind::RasterLayer;
    previewEvidence->query.time.explicitYears = {2022};
    previewEvidence->raster.bounds = {1.1, 2.2, 3.3, 4.4};
    previewEvidence->raster.sourceResolutionMeters = 10.5;
    previewEvidence->raster.displayResolutionMeters = 20.5;
    earthscience::ScienceSourceReference previewReference;
    previewReference.actualCoverage = {5.5, 6.6, 7.7, 8.8};
    previewEvidence->sourceReferences.push_back(previewReference);
    const std::string previewEvidenceText = joined(
        describeScienceArtifactEvidence(*previewEvidence));
    CHECK(previewEvidenceText.find("2022") != std::string::npos);
    CHECK(previewEvidenceText.find("1.1000") != std::string::npos);
    CHECK(previewEvidenceText.find("5.5000") != std::string::npos);
    CHECK(previewEvidenceText.find("10.5 m") != std::string::npos);
    CHECK(previewEvidenceText.find("20.5 m") != std::string::npos);

    std::shared_ptr<earthscience::ScienceArtifact> pointEvidence =
        std::make_shared<earthscience::ScienceArtifact>();
    pointEvidence->query.outputKind =
        earthscience::ScienceOutputKind::TimeSeries;
    pointEvidence->query.time.explicitYears = {2017, 2025};
    pointEvidence->analysis.kind =
        earthscience::ScienceAnalysisKind::PointSeries;
    pointEvidence->embedding.bounds = {11.1, 12.2, 13.3, 14.4};
    pointEvidence->embedding.actualResolutionMeters = 12.5;
    earthscience::ScienceSourceReference pointReference;
    pointReference.actualCoverage = {15.5, 16.6, 17.7, 18.8};
    pointEvidence->sourceReferences.push_back(pointReference);
    const std::string pointEvidenceText = joined(
        describeScienceArtifactEvidence(*pointEvidence));
    CHECK(pointEvidenceText.find("2017, 2025") != std::string::npos);
    CHECK(pointEvidenceText.find("11.1000") != std::string::npos);
    CHECK(pointEvidenceText.find("15.5000") != std::string::npos);
    CHECK(pointEvidenceText.find("12.5 m") != std::string::npos);

    std::shared_ptr<earthscience::ScienceArtifact> regionalEvidence =
        std::make_shared<earthscience::ScienceArtifact>();
    regionalEvidence->query.outputKind =
        earthscience::ScienceOutputKind::Analysis;
    regionalEvidence->query.time.explicitYears = {2018, 2024};
    regionalEvidence->analysis.kind =
        earthscience::ScienceAnalysisKind::RegionalChange;
    regionalEvidence->analysis.regionalChange.bounds =
        {21.1, 22.2, 23.3, 24.4};
    regionalEvidence->analysis.regionalChange.actualResolutionMeters = 22.5;
    earthscience::ScienceSourceReference regionalReference;
    regionalReference.actualCoverage = {25.5, 26.6, 27.7, 28.8};
    regionalEvidence->sourceReferences.push_back(regionalReference);
    const std::string regionalEvidenceText = joined(
        describeScienceArtifactEvidence(*regionalEvidence));
    CHECK(regionalEvidenceText.find("2018, 2024") != std::string::npos);
    CHECK(regionalEvidenceText.find("21.1000") != std::string::npos);
    CHECK(regionalEvidenceText.find("25.5000") != std::string::npos);
    CHECK(regionalEvidenceText.find("22.5 m") != std::string::npos);

    SciencePanelState changedInput = panel.state();
    changedInput.lastYear = 2025;
    panel.setStateForTest(changedInput);
    previewReplacementFailure.lastSuccessfulPreviewArtifact = previewEvidence;
    const std::shared_ptr<const earthscience::ScienceArtifact> retainedYear =
        selectSciencePanelArtifact(
            previewReplacementFailure, SciencePanelMode::Preview);
    CHECK(retainedYear);
    const std::string retainedYearText = joined(
        describeScienceArtifactEvidence(*retainedYear));
    CHECK(retainedYearText.find("2022") != std::string::npos);
    CHECK(retainedYearText.find("2025") == std::string::npos);

    std::cout << "[OK] ScienceEarth progressive panel state\n";
    return EXIT_SUCCESS;
}
