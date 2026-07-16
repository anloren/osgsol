#include "science_earth_panel.h"

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
    CHECK(defaults.locationMode == SciencePanelLocationMode::CurrentLocation);
    CHECK(defaults.firstYear == 2017);
    CHECK(defaults.lastYear == 2025);
    CHECK(defaults.baselineYear == 2017);
    CHECK(defaults.comparisonYear == 2025);
    CHECK(defaults.gridSize == 128);
    CHECK(!defaults.advancedOpen);
    CHECK(defaults.resultExpanded);
    CHECK(!defaults.enablePca);
    CHECK(!defaults.enableClustering);

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
    CHECK(retained.kind == SciencePanelResultKind::RetainedPointSeries);
    CHECK(retained.detail.find("network timeout") != std::string::npos);
    CHECK(!retained.retentionReason.empty());
    CHECK(retained.retentionReason.find("retained-analysis") != std::string::npos);
    CHECK(retained.detail != retained.retentionReason);

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
    CHECK(retainedPreview.kind == SciencePanelResultKind::RetainedPreview);
    CHECK(retainedPreview.severity == SciencePanelSeverity::Warning);
    CHECK(retainedPreview.title.find("Preview") != std::string::npos);
    CHECK(retainedPreview.retentionReason.find("preview-2022") !=
          std::string::npos);
    CHECK(retainedPreview.retentionReason.find("point-series") ==
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
    CHECK(retainedPoint.kind == SciencePanelResultKind::RetainedPointSeries);
    CHECK(retainedPoint.title.find("Point series") != std::string::npos);
    CHECK(retainedPoint.retentionReason.find("point-retained") !=
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
    CHECK(retainedRegional.kind ==
          SciencePanelResultKind::RetainedRegionalChange);
    CHECK(retainedRegional.title.find("Regional change") != std::string::npos);
    CHECK(retainedRegional.retentionReason.find("regional-retained") !=
          std::string::npos);

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
