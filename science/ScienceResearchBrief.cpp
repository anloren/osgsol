#include "ScienceResearchBrief.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace earthscience
{
namespace
{
    std::string selectedTime(const ScienceEvidenceRecord& evidence)
    {
        if (!evidence.acquisitionTime.empty()) return evidence.acquisitionTime;
        if (!evidence.publicationTime.empty()) return evidence.publicationTime;
        if (!evidence.forecastReferenceTime.empty())
            return evidence.forecastReferenceTime;
        return "not recorded";
    }

    std::string compactNumber(double value, int precision = 3)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        std::string result = stream.str();
        while (result.size() > 1 && result.back() == '0') result.pop_back();
        if (!result.empty() && result.back() == '.') result.pop_back();
        if (result == "-0") result = "0";
        return result;
    }

    std::string inlineMarkdown(const std::string& input)
    {
        std::string result;
        result.reserve(input.size());
        for (char byte : input)
        {
            if (byte == '\r' || byte == '\n') result.push_back(' ');
            else
            {
                if (byte == '|') result.push_back('\\');
                result.push_back(byte);
            }
        }
        return result;
    }

    bool boundsOverlap(
        const ScienceWgs84Bounds& left, const ScienceWgs84Bounds& right)
    {
        return std::max(left.west, right.west) <
                std::min(left.east, right.east) &&
            std::max(left.south, right.south) <
                std::min(left.north, right.north);
    }

    bool allCoverageOverlaps(
        const std::vector<const ScienceEvidenceRecord*>& evidence)
    {
        if (evidence.size() < 2) return false;
        ScienceWgs84Bounds intersection = evidence.front()->actualCoverage;
        for (std::size_t i = 1; i < evidence.size(); ++i)
        {
            if (!boundsOverlap(intersection, evidence[i]->actualCoverage))
                return false;
            intersection.west = std::max(
                intersection.west, evidence[i]->actualCoverage.west);
            intersection.south = std::max(
                intersection.south, evidence[i]->actualCoverage.south);
            intersection.east = std::min(
                intersection.east, evidence[i]->actualCoverage.east);
            intersection.north = std::min(
                intersection.north, evidence[i]->actualCoverage.north);
        }
        return true;
    }

    ScienceBriefStatement alphaObservation(
        const ScienceEvidenceRecord& evidence)
    {
        ScienceBriefStatement statement;
        statement.label = "Observation";
        statement.evidenceIds = {evidence.evidenceId};
        std::ostringstream text;
        text << "AlphaEarth evidence describes a 64-dimensional latent "
             << "representation";
        if (!evidence.primaryMetrics.empty())
        {
            const ScienceEvidenceMetric& metric =
                evidence.primaryMetrics.front();
            text << " and records " << scienceMetricName(metric.metric)
                 << " " << compactNumber(metric.value);
            if (!metric.unit.empty()) text << ' ' << metric.unit;
            if (metric.baselineYear != 0 || metric.comparisonYear != 0)
                text << " for " << metric.baselineYear << " to "
                     << metric.comparisonYear;
        }
        text << ". Its components are not named physical variables or named "
             << "land-cover variables and do not identify a cause.";
        statement.text = text.str();
        return statement;
    }

    ScienceBriefStatement sentinelObservation(
        const ScienceEvidenceRecord& evidence)
    {
        ScienceBriefStatement statement;
        statement.label = "Observation";
        statement.evidenceIds = {evidence.evidenceId};
        statement.text = "Sentinel-2 natural-color evidence represents the "
            "selected acquisition " + selectedTime(evidence) +
            " and its scene-wide cloud metadata. It is not a cloud-free "
            "composite or, by itself, proof of change.";
        return statement;
    }

    ScienceBriefStatement demObservation(
        const ScienceEvidenceRecord& evidence)
    {
        ScienceBriefStatement statement;
        statement.label = "Observation";
        statement.evidenceIds = {evidence.evidenceId};
        std::ostringstream text;
        text << "Copernicus DEM is static EGM2008 DSM surface-elevation "
             << "evidence, not bare-earth terrain";
        const auto found = std::find_if(
            evidence.scalarSummaries.begin(),
            evidence.scalarSummaries.end(),
            [](const ScienceScalarSummary& value)
            { return value.variableId == "surface_elevation"; });
        if (found == evidence.scalarSummaries.end())
            text << "; no numeric surface-elevation summary is available, "
                 << "so display colors are not interpreted";
        else
        {
            text << ".";
            if (found->centerValid)
                text << " Center: " << compactNumber(found->center)
                     << ' ' << found->unit << ".";
            if (found->meanValid)
                text << " Mean: " << compactNumber(found->mean)
                     << ' ' << found->unit << ".";
            if (found->minimumValid && found->maximumValid)
                text << " Range: " << compactNumber(found->minimum)
                     << " to " << compactNumber(found->maximum)
                     << ' ' << found->unit << ".";
        }
        statement.text = text.str();
        return statement;
    }

    ScienceBriefStatement genericObservation(
        const ScienceEvidenceRecord& evidence)
    {
        ScienceBriefStatement statement;
        statement.label = "Observation";
        statement.evidenceIds = {evidence.evidenceId};
        statement.text = evidence.sourceName + " supplies " +
            std::to_string(evidence.validCellCount) +
            " valid cells for the recorded coverage.";
        return statement;
    }

    std::string citationSuffix(
        const ScienceBriefStatement& statement,
        const std::map<std::string, std::size_t>& numbers)
    {
        std::string result;
        std::set<std::size_t> emitted;
        for (const std::string& id : statement.evidenceIds)
        {
            const auto found = numbers.find(id);
            if (found != numbers.end() && emitted.insert(found->second).second)
                result += "[" + std::to_string(found->second) + "]";
        }
        return result;
    }

    void renderStatements(
        std::ostringstream& markdown,
        const std::vector<ScienceBriefStatement>& statements,
        const std::map<std::string, std::size_t>& numbers)
    {
        if (statements.empty())
        {
            markdown << "- None supported by the available evidence.\n";
            return;
        }
        for (const ScienceBriefStatement& statement : statements)
            markdown << "- **" << inlineMarkdown(statement.label) << ":** "
                     << inlineMarkdown(statement.text) << ' '
                     << citationSuffix(statement, numbers) << "\n";
    }

    bool statementsCovered(
        const std::vector<ScienceBriefStatement>& statements,
        const std::set<std::string>& known, std::string& error)
    {
        for (const ScienceBriefStatement& statement : statements)
        {
            if (statement.text.empty() || statement.evidenceIds.empty())
            {
                error = "material brief statement is missing evidence";
                return false;
            }
            for (const std::string& id : statement.evidenceIds)
                if (known.count(id) == 0)
                {
                    error = "brief statement references missing evidence";
                    return false;
                }
        }
        return true;
    }
}

bool buildScienceResearchBrief(
    const ScienceResearchRecord& research,
    const std::vector<ScienceEvidenceRecord>& suppliedEvidence,
    ScienceResearchBrief& brief, std::string& error)
{
    error.clear();
    if (!validateScienceResearch(research, error)) return false;

    std::map<std::string, const ScienceEvidenceRecord*> byId;
    for (const ScienceEvidenceRecord& evidence : suppliedEvidence)
    {
        if (!validateScienceEvidence(evidence, error)) return false;
        if (!byId.emplace(evidence.evidenceId, &evidence).second)
        {
            error = "duplicate supplied evidence id";
            return false;
        }
    }

    brief = ScienceResearchBrief{};
    brief.researchId = research.researchId;
    brief.question = research.question;
    brief.state = research.state;
    std::vector<const ScienceEvidenceRecord*> ordered;
    std::vector<const ScienceResearchStep*> failed;
    for (const ScienceResearchStep& step : research.steps)
    {
        if (step.evidenceId.empty())
        {
            if (step.state == ScienceJobState::Failed ||
                step.state == ScienceJobState::Cancelled)
                failed.push_back(&step);
            continue;
        }
        const auto found = byId.find(step.evidenceId);
        if (found == byId.end())
        {
            error = "research step references missing supplied evidence";
            return false;
        }
        if (found->second->sourceId != step.sourceId)
        {
            error = "research step and evidence source do not match";
            return false;
        }
        ordered.push_back(found->second);
    }

    std::map<std::string, std::size_t> citationNumbers;
    std::set<std::string> knownEvidence;
    for (const ScienceEvidenceRecord* evidence : ordered)
    {
        ScienceBriefCitation citation;
        citation.number = brief.citations.size() + 1;
        citation.evidenceId = evidence->evidenceId;
        citation.sourceName = evidence->sourceName;
        citation.datasetId = evidence->datasetId;
        citation.providerVersion = evidence->providerVersion;
        citation.originalUrl = evidence->originalUrl;
        citation.attribution = evidence->attribution;
        citationNumbers[citation.evidenceId] = citation.number;
        knownEvidence.insert(citation.evidenceId);
        brief.citations.push_back(citation);

        ScienceBriefSourceRow row;
        row.citationNumber = citation.number;
        row.evidenceId = evidence->evidenceId;
        row.sourceId = evidence->sourceId;
        row.sourceName = evidence->sourceName;
        row.datasetId = evidence->datasetId;
        row.selectedTime = selectedTime(*evidence);
        row.coverage = evidence->actualCoverage;
        brief.sources.push_back(row);

        if (evidence->sourceId == "alphaearth-foundations")
            brief.observations.push_back(alphaObservation(*evidence));
        else if (evidence->sourceId == "sentinel-2-l2a")
            brief.observations.push_back(sentinelObservation(*evidence));
        else if (evidence->sourceId == "copernicus-dem-glo-30")
            brief.observations.push_back(demObservation(*evidence));
        else
            brief.observations.push_back(genericObservation(*evidence));

        for (const std::string& value : evidence->interpretations)
            if (!value.empty())
                brief.observations.push_back(
                    {"Analysis interpretation", value,
                     {evidence->evidenceId}});
        for (const std::string& value : evidence->warnings)
            if (!value.empty())
                brief.limitations.push_back(
                    {"Source warning", value, {evidence->evidenceId}});
        for (const std::string& value : evidence->limitations)
            if (!value.empty())
                brief.limitations.push_back(
                    {"Source limitation", value, {evidence->evidenceId}});
    }

    if (ordered.size() >= 2)
    {
        std::vector<std::string> allIds;
        for (const ScienceEvidenceRecord* evidence : ordered)
            allIds.push_back(evidence->evidenceId);
        if (allCoverageOverlaps(ordered))
        {
            brief.inferences.push_back({
                "Inference",
                "The cited sources cover a common ground area and may be "
                "compared as complementary latent, visible-image, and static "
                "surface-elevation evidence; spatial agreement alone does "
                "not establish cause.",
                allIds});
        }
        else
        {
            brief.limitations.push_back({
                "Spatial limitation",
                "The common actual source coverage does not overlap as one "
                "area, so no cross-source inference is emitted.",
                allIds});
        }

        std::set<std::string> times;
        for (const ScienceEvidenceRecord* evidence : ordered)
        {
            const std::string time = selectedTime(*evidence);
            if (time != "not recorded") times.insert(time);
        }
        if (times.size() > 1)
            brief.limitations.push_back({
                "Temporal mismatch",
                "Temporal mismatch is explicit: the cited sources do not "
                "share one acquisition or reference time, and the static "
                "DEM cannot demonstrate temporal change.",
                allIds});
    }

    if (!statementsCovered(
            brief.observations, knownEvidence, error) ||
        !statementsCovered(brief.inferences, knownEvidence, error) ||
        !statementsCovered(brief.limitations, knownEvidence, error))
        return false;

    std::ostringstream markdown;
    markdown << "# ScienceEarth research brief\n\n"
             << "## Scope and status\n\n"
             << "- Research ID: `" << inlineMarkdown(brief.researchId)
             << "`\n"
             << "- Question: " << inlineMarkdown(brief.question) << "\n"
             << "- Status: **" << scienceResearchStateName(brief.state)
             << "**\n";
    for (const ScienceResearchStep* step : failed)
        markdown << "- Missing source `" << inlineMarkdown(step->sourceId)
                 << "`: " << inlineMarkdown(step->message) << "\n";

    markdown << "\n## Source, time, and coverage\n\n"
             << "| Ref | Source | Dataset | Time | WGS84 coverage |\n"
             << "|---:|---|---|---|---|\n";
    for (const ScienceBriefSourceRow& row : brief.sources)
        markdown << "| [" << row.citationNumber << "] | "
                 << inlineMarkdown(row.sourceName) << " (`"
                 << inlineMarkdown(row.sourceId) << "`) | "
                 << inlineMarkdown(row.datasetId) << " | "
                 << inlineMarkdown(row.selectedTime) << " | "
                 << compactNumber(row.coverage.west, 6) << ", "
                 << compactNumber(row.coverage.south, 6) << " to "
                 << compactNumber(row.coverage.east, 6) << ", "
                 << compactNumber(row.coverage.north, 6) << " |\n";

    markdown << "\n## Observations\n\n";
    renderStatements(markdown, brief.observations, citationNumbers);
    markdown << "\n## Cross-source inferences\n\n";
    renderStatements(markdown, brief.inferences, citationNumbers);
    markdown << "\n## Limitations\n\n";
    renderStatements(markdown, brief.limitations, citationNumbers);
    markdown << "\n## Citations\n\n";
    for (const ScienceBriefCitation& citation : brief.citations)
    {
        markdown << '[' << citation.number << "] "
                 << inlineMarkdown(citation.sourceName) << "; dataset `"
                 << inlineMarkdown(citation.datasetId) << "`; provider `"
                 << inlineMarkdown(citation.providerVersion) << "`";
        if (!citation.originalUrl.empty())
            markdown << "; " << inlineMarkdown(citation.originalUrl);
        markdown << "; " << inlineMarkdown(citation.attribution)
                 << "; evidence `" << inlineMarkdown(citation.evidenceId)
                 << "`.\n";
    }
    brief.markdown = markdown.str();
    return true;
}
}
