#include "science_earth_panel.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

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
    return artifact;
}

std::shared_ptr<const earthscience::ScienceArtifact> artifact(
    const std::string& id)
{
    std::shared_ptr<earthscience::ScienceArtifact> value =
        std::make_shared<earthscience::ScienceArtifact>();
    value->artifactId = id;
    return value;
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
        describeScienceSnapshot(replacementFailure);
    CHECK(retained.detail.find("network timeout") != std::string::npos);
    CHECK(!retained.retentionReason.empty());
    CHECK(retained.retentionReason.find("retained-analysis") != std::string::npos);
    CHECK(retained.detail != retained.retentionReason);

    earthscience::ScienceJobSnapshot independentResults;
    independentResults.lastSuccessfulPreviewArtifact = artifact("preview-new");
    independentResults.lastSuccessfulAnalysisArtifact = artifact("analysis-old");
    independentResults.displayArtifact =
        independentResults.lastSuccessfulPreviewArtifact;
    CHECK(selectSciencePanelArtifact(
        independentResults, SciencePanelMode::Preview)->artifactId ==
        "preview-new");
    CHECK(selectSciencePanelArtifact(
        independentResults, SciencePanelMode::PointSeries)->artifactId ==
        "analysis-old");

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

    std::cout << "[OK] ScienceEarth progressive panel state\n";
    return EXIT_SUCCESS;
}
