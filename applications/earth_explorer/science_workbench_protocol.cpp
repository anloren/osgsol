#include "science_workbench_protocol.h"

#include <picojson.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

namespace
{
const char* phaseName(ScienceWorkbenchPhase phase)
{
    switch (phase)
    {
    case ScienceWorkbenchPhase::Draft: return "draft";
    case ScienceWorkbenchPhase::TargetLocked: return "target-locked";
    case ScienceWorkbenchPhase::ReadyToRun: return "ready-to-run";
    case ScienceWorkbenchPhase::Queued: return "queued";
    case ScienceWorkbenchPhase::Fetching: return "fetching";
    case ScienceWorkbenchPhase::Analyzing: return "analyzing";
    case ScienceWorkbenchPhase::Ready: return "ready";
    case ScienceWorkbenchPhase::Failed: return "failed";
    case ScienceWorkbenchPhase::Cancelled: return "cancelled";
    }
    return "draft";
}

bool validUtf8(const char* text, std::size_t size)
{
    if (!text && size != 0) return false;
    std::size_t index = 0;
    while (index < size)
    {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7f)
        {
            ++index;
            continue;
        }
        std::size_t continuation = 0;
        std::uint32_t codePoint = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            continuation = 1;
            codePoint = first & 0x1f;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            continuation = 2;
            codePoint = first & 0x0f;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            continuation = 3;
            codePoint = first & 0x07;
        }
        else return false;
        if (index + continuation >= size) return false;
        for (std::size_t offset = 1; offset <= continuation; ++offset)
        {
            const unsigned char next =
                static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xc0) != 0x80) return false;
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if ((continuation == 2 && codePoint < 0x800) ||
            (continuation == 3 && codePoint < 0x10000) ||
            codePoint > 0x10ffff ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff))
            return false;
        index += continuation + 1;
    }
    return true;
}

picojson::value number(double value)
{
    return picojson::value(std::isfinite(value) ? value : 0.0);
}

picojson::value availableNumber(double value, bool available)
{
    return available && std::isfinite(value)
        ? picojson::value(value) : picojson::value();
}

picojson::value availableString(const std::string& value)
{
    return value.empty() ? picojson::value() : picojson::value(value);
}

picojson::object pointObject(const earthscience::ScienceWgs84Point& point)
{
    picojson::object output;
    output["latitude"] = number(point.latitude);
    output["longitude"] = number(point.longitude);
    return output;
}

picojson::object boundsObject(const earthscience::ScienceWgs84Bounds& bounds)
{
    picojson::object output;
    output["west"] = number(bounds.west);
    output["south"] = number(bounds.south);
    output["east"] = number(bounds.east);
    output["north"] = number(bounds.north);
    return output;
}

picojson::object geometryObject(const earthscience::ScienceGeometry& geometry)
{
    picojson::object output;
    if (geometry.kind == earthscience::ScienceGeometryKind::Point)
    {
        output["kind"] = picojson::value("point");
        output["point"] = picojson::value(pointObject(geometry.point));
    }
    else
    {
        output["kind"] = picojson::value("bounds");
        output["bounds"] = picojson::value(boundsObject(geometry.bounds));
    }
    output["requestedSpanMeters"] = number(geometry.requestedSpanMeters);
    return output;
}

picojson::array stringArray(const std::vector<std::string>& values)
{
    picojson::array output;
    output.reserve(values.size());
    for (const std::string& value : values)
        output.push_back(picojson::value(value));
    return output;
}

picojson::object variableObject(
    const earthscience::ScienceVariableDescriptor& variable)
{
    picojson::object output;
    output["id"] = picojson::value(variable.id);
    output["name"] = picojson::value(variable.displayName);
    output["unit"] = picojson::value(variable.unit);
    output["aggregation"] = picojson::value(variable.aggregationMethod);
    output["scientificMeaning"] = picojson::value(variable.scientificMeaning);
    output["uncertainty"] = picojson::value(variable.uncertaintyStatement);
    output["nativeResolutionMeters"] = number(variable.nativeResolutionMeters);
    return output;
}

picojson::object sourceObject(
    const earthscience::ScienceSourceDescriptor& source)
{
    picojson::object output;
    output["id"] = picojson::value(source.id);
    output["name"] = picojson::value(source.name);
    output["category"] = picojson::value(source.category);
    output["providerVersion"] = picojson::value(source.providerVersion);
    output["firstYear"] = number(source.firstYear);
    output["lastYear"] = number(source.lastYear);
    output["nativeResolutionMeters"] = number(source.nativeResolutionMeters);
    output["spatialSupport"] = picojson::value(source.spatialSupport);
    output["temporalResolution"] = picojson::value(source.temporalResolution);
    output["license"] = picojson::value(source.license);
    output["documentationUrl"] = picojson::value(source.documentationUrl);
    output["attribution"] = picojson::value(source.attribution);
    output["qualityStatement"] = picojson::value(source.qualityStatement);
    output["dataNature"] = picojson::value(source.dataNature);
    output["updateLatency"] = picojson::value(source.updateLatency);
    output["health"] = picojson::value(
        earthscience::scienceSourceHealthName(source.health));
    output["healthMessage"] = picojson::value(source.healthMessage);
    output["experimental"] = picojson::value(source.experimental);

    picojson::object capabilities;
    capabilities["point"] = picojson::value(source.capabilities.pointQuery);
    capabilities["bounds"] = picojson::value(
        source.capabilities.boundingBoxQuery);
    capabilities["currentView"] = picojson::value(
        source.capabilities.currentViewQuery);
    capabilities["explicitYears"] = picojson::value(
        source.capabilities.explicitYears);
    capabilities["timeSeries"] = picojson::value(
        source.capabilities.timeSeriesOutput);
    capabilities["analysis"] = picojson::value(
        source.capabilities.analysisOutput);
    capabilities["export"] = picojson::value(
        source.capabilities.exportOutput);
    capabilities["raster"] = picojson::value(
        source.capabilities.rasterLayerOutput);
    capabilities["table"] = picojson::value(
        source.capabilities.tableOutput);
    output["capabilities"] = picojson::value(capabilities);

    picojson::array variables;
    variables.reserve(source.variables.size());
    for (const earthscience::ScienceVariableDescriptor& variable :
         source.variables)
        variables.push_back(picojson::value(variableObject(variable)));
    output["variables"] = picojson::value(variables);
    return output;
}

picojson::object costObject(const earthscience::ScienceQueryCost& cost)
{
    picojson::object output;
    output["sourceBytesUpperBound"] = number(
        static_cast<double>(cost.sourceBytesUpperBound));
    output["residentBytesUpperBound"] = number(
        static_cast<double>(cost.residentBytesUpperBound));
    output["resultCells"] = number(static_cast<double>(cost.resultCells));
    output["estimatedDurationSeconds"] = number(
        cost.estimatedDurationSeconds);
    output["durationDeterminate"] = picojson::value(
        cost.durationDeterminate);
    output["requiresConfirmation"] = picojson::value(
        cost.requiresConfirmation);
    return output;
}

picojson::object progressObject(const earthscience::ScienceProgress& progress)
{
    picojson::object output;
    output["stage"] = picojson::value(
        earthscience::scienceProgressStageName(progress.stage));
    output["completedUnits"] = number(
        static_cast<double>(progress.completedUnits));
    output["totalUnits"] = number(static_cast<double>(progress.totalUnits));
    output["determinate"] = picojson::value(progress.determinate);
    output["unit"] = picojson::value(progress.unit);
    output["elapsedSeconds"] = number(progress.elapsedSeconds);
    return output;
}

picojson::object seriesObject(
    const earthscience::ScienceVariableSeries& series)
{
    picojson::object output;
    output["id"] = picojson::value(series.variableId);
    output["name"] = picojson::value(series.displayName);
    output["unit"] = picojson::value(series.unit);
    output["aggregation"] = picojson::value(series.aggregationMethod);
    output["nativeResolutionMeters"] = number(series.nativeResolutionMeters);
    picojson::array points;
    if (series.years && series.values)
    {
        const std::size_t count = std::min(
            series.years->size(), series.values->size());
        points.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            picojson::object point;
            point["year"] = number((*series.years)[index]);
            const bool valid = (!series.validity ||
                index >= series.validity->size() ||
                (*series.validity)[index] != 0) &&
                std::isfinite((*series.values)[index]);
            point["valid"] = picojson::value(valid);
            if (valid) point["value"] = number((*series.values)[index]);
            points.push_back(picojson::value(point));
        }
    }
    output["points"] = picojson::value(points);
    return output;
}

picojson::object referenceObject(
    const earthscience::ScienceSourceReference& reference)
{
    picojson::object output;
    output["sourceId"] = picojson::value(reference.sourceId);
    output["providerVersion"] = picojson::value(reference.providerVersion);
    output["datasetId"] = picojson::value(reference.datasetId);
    output["originalUrl"] = picojson::value(reference.originalUrl);
    output["requestedCoverage"] = picojson::value(
        geometryObject(reference.requestedCoverage));
    output["actualCoverage"] = picojson::value(
        boundsObject(reference.actualCoverage));
    output["variables"] = picojson::value(stringArray(reference.variables));
    output["units"] = picojson::value(stringArray(reference.units));
    output["processingSteps"] = picojson::value(
        stringArray(reference.processingSteps));
    output["acquisitionTime"] = availableString(reference.acquisitionTime);
    output["publicationTime"] = availableString(reference.publicationTime);
    output["forecastReferenceTime"] = availableString(
        reference.forecastReferenceTime);
    output["attribution"] = picojson::value(reference.attribution);
    return output;
}

bool validCoverage(const earthscience::ScienceWgs84Bounds& bounds)
{
    return std::isfinite(bounds.west) && std::isfinite(bounds.south) &&
        std::isfinite(bounds.east) && std::isfinite(bounds.north) &&
        bounds.west < bounds.east && bounds.south < bounds.north &&
        bounds.west >= -180.0 && bounds.east <= 180.0 &&
        bounds.south >= -90.0 && bounds.north <= 90.0;
}

picojson::object reportEvidenceObject(
    const ScienceWorkbenchViewModel& model,
    const std::vector<earthscience::ScienceSourceDescriptor>& sources,
    const std::shared_ptr<const earthscience::ScienceArtifact>& artifact)
{
    picojson::object output;
    output["availability"] = picojson::value(
        artifact ? "ready" : "unavailable");
    output["requestedGeometry"] = artifact
        ? picojson::value(geometryObject(artifact->query.geometry))
        : picojson::value();

    const earthscience::ScienceSourceReference* firstReference =
        artifact && !artifact->sourceReferences.empty()
            ? &artifact->sourceReferences.front() : nullptr;
    output["actualCoverage"] = firstReference &&
        validCoverage(firstReference->actualCoverage)
            ? picojson::value(boundsObject(firstReference->actualCoverage))
            : picojson::value();

    const earthscience::ScienceSourceDescriptor* source = nullptr;
    const std::string sourceId = artifact
        ? artifact->query.sourceId : model.draft.sourceId;
    for (const earthscience::ScienceSourceDescriptor& candidate : sources)
        if (candidate.id == sourceId) { source = &candidate; break; }
    output["sourceNativeResolutionMeters"] = availableNumber(
        source ? source->nativeResolutionMeters : 0.0,
        source && source->nativeResolutionMeters > 0.0);
    output["spatialSupport"] = source
        ? availableString(source->spatialSupport) : picojson::value();
    output["sourceLicense"] = source
        ? availableString(source->license) : picojson::value();
    output["sourceDocumentationUrl"] = source
        ? availableString(source->documentationUrl) : picojson::value();
    output["sourceAttribution"] = source
        ? availableString(source->attribution) : picojson::value();
    output["sourceQualityStatement"] = source
        ? availableString(source->qualityStatement) : picojson::value();

    picojson::array aggregations;
    picojson::array completeness;
    if (artifact)
    {
        std::vector<std::string> seenAggregations;
        for (const earthscience::ScienceVariableSeries& series :
             artifact->variableSeries)
        {
            if (!series.aggregationMethod.empty() &&
                std::find(seenAggregations.begin(), seenAggregations.end(),
                          series.aggregationMethod) == seenAggregations.end())
            {
                seenAggregations.push_back(series.aggregationMethod);
                aggregations.push_back(picojson::value(
                    series.aggregationMethod));
            }
            picojson::object item;
            item["metricId"] = picojson::value(series.variableId);
            std::size_t total = 0, valid = 0;
            if (series.years && series.values)
            {
                total = std::min(series.years->size(), series.values->size());
                for (std::size_t index = 0; index < total; ++index)
                {
                    const bool pointValid = std::isfinite(
                        (*series.values)[index]) &&
                        (!series.validity || index >= series.validity->size() ||
                         (*series.validity)[index] != 0);
                    if (pointValid) ++valid;
                }
            }
            item["validPoints"] = number(static_cast<double>(valid));
            item["totalPoints"] = number(static_cast<double>(total));
            item["fraction"] = availableNumber(
                total ? static_cast<double>(valid) /
                    static_cast<double>(total) : 0.0, total > 0);
            completeness.push_back(picojson::value(item));
        }
    }
    output["aggregationMethods"] = picojson::value(aggregations);
    output["seriesCompleteness"] = picojson::value(completeness);

    picojson::array limitations;
    const bool limitationsProvided = artifact &&
        static_cast<bool>(artifact->analysis.limitations);
    if (limitationsProvided)
        limitations = stringArray(*artifact->analysis.limitations);
    output["limitations"] = picojson::value(limitations);
    output["limitationsAvailability"] = picojson::value(
        limitationsProvided ? "provided" : "unavailable");
    output["warnings"] = picojson::value(
        artifact ? stringArray(artifact->warnings) : picojson::array());

    picojson::array citations;
    if (artifact)
        for (const earthscience::ScienceSourceReference& reference :
             artifact->sourceReferences)
            citations.push_back(picojson::value(referenceObject(reference)));
    output["citations"] = picojson::value(citations);
    output["citationAvailability"] = picojson::value(
        citations.empty() ? "unavailable" : "provided");
    output["executionTimingSeconds"] = availableNumber(
        model.progress.elapsedSeconds, model.progress.elapsedSeconds > 0.0);

    if (source)
    {
        picojson::object exports;
        exports["artifactExport"] = picojson::value(
            source->capabilities.exportOutput);
        exports["rasterOutput"] = picojson::value(
            source->capabilities.rasterLayerOutput);
        exports["tableOutput"] = picojson::value(
            source->capabilities.tableOutput);
        output["exportCapabilities"] = picojson::value(exports);
    }
    else output["exportCapabilities"] = picojson::value();
    return output;
}

picojson::object artifactObject(
    const earthscience::ScienceArtifact& artifact)
{
    picojson::object output;
    output["artifactId"] = picojson::value(artifact.artifactId);
    output["sourceId"] = picojson::value(artifact.query.sourceId);
    output["queryGeometry"] = picojson::value(
        geometryObject(artifact.query.geometry));
    output["warnings"] = picojson::value(stringArray(artifact.warnings));
    output["processingVersion"] = picojson::value(
        artifact.processingVersion);
    output["createdAt"] = picojson::value(artifact.createdAt);

    picojson::array series;
    series.reserve(artifact.variableSeries.size());
    for (const earthscience::ScienceVariableSeries& value :
         artifact.variableSeries)
        series.push_back(picojson::value(seriesObject(value)));
    output["series"] = picojson::value(series);

    picojson::array references;
    references.reserve(artifact.sourceReferences.size());
    for (const earthscience::ScienceSourceReference& reference :
         artifact.sourceReferences)
        references.push_back(picojson::value(referenceObject(reference)));
    output["sourceReferences"] = picojson::value(references);

    picojson::array limitations;
    if (artifact.analysis.limitations)
        limitations = stringArray(*artifact.analysis.limitations);
    output["limitations"] = picojson::value(limitations);
    return output;
}

bool getString(const picojson::object& object, const char* key,
               std::string& output)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->second.is<std::string>())
        return false;
    output = found->second.get<std::string>();
    return true;
}

bool getInteger(const picojson::object& object, const char* key, int& output)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->second.is<double>()) return false;
    const double value = found->second.get<double>();
    if (!std::isfinite(value) || std::floor(value) != value ||
        value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max())
        return false;
    output = static_cast<int>(value);
    return true;
}

bool parseGeometry(const picojson::object& object,
                   earthscience::ScienceGeometry& geometry)
{
    std::string kind;
    if (!getString(object, "kind", kind)) return false;
    if (kind == "point")
    {
        const auto found = object.find("point");
        if (found == object.end() || !found->second.is<picojson::object>())
            return false;
        const picojson::object& point = found->second.get<picojson::object>();
        const auto latitude = point.find("latitude");
        const auto longitude = point.find("longitude");
        if (latitude == point.end() || longitude == point.end() ||
            !latitude->second.is<double>() || !longitude->second.is<double>())
            return false;
        geometry.kind = earthscience::ScienceGeometryKind::Point;
        geometry.point.latitude = latitude->second.get<double>();
        geometry.point.longitude = longitude->second.get<double>();
        return std::isfinite(geometry.point.latitude) &&
               std::isfinite(geometry.point.longitude) &&
               geometry.point.latitude >= -90.0 &&
               geometry.point.latitude <= 90.0 &&
               geometry.point.longitude >= -180.0 &&
               geometry.point.longitude <= 180.0;
    }
    if (kind != "bounds") return false;
    const auto found = object.find("bounds");
    if (found == object.end() || !found->second.is<picojson::object>())
        return false;
    const picojson::object& bounds = found->second.get<picojson::object>();
    const auto west = bounds.find("west");
    const auto south = bounds.find("south");
    const auto east = bounds.find("east");
    const auto north = bounds.find("north");
    if (west == bounds.end() || south == bounds.end() ||
        east == bounds.end() || north == bounds.end() ||
        !west->second.is<double>() || !south->second.is<double>() ||
        !east->second.is<double>() || !north->second.is<double>())
        return false;
    geometry.kind = earthscience::ScienceGeometryKind::BoundingBox;
    geometry.bounds = {west->second.get<double>(), south->second.get<double>(),
                       east->second.get<double>(), north->second.get<double>()};
    return std::isfinite(geometry.bounds.west) &&
           std::isfinite(geometry.bounds.south) &&
           std::isfinite(geometry.bounds.east) &&
           std::isfinite(geometry.bounds.north) &&
           geometry.bounds.west < geometry.bounds.east &&
           geometry.bounds.south < geometry.bounds.north &&
           geometry.bounds.south >= -90.0 && geometry.bounds.north <= 90.0;
}
}

std::string serializeScienceWorkbenchSnapshot(
    const ScienceWorkbenchViewModel& model,
    const std::vector<earthscience::ScienceSourceDescriptor>& sources,
    std::shared_ptr<const earthscience::ScienceArtifact> activeArtifact)
{
    picojson::object root;
    root["schema"] = picojson::value("science-workbench-ui-v1");
    root["revision"] = number(static_cast<double>(model.revision));
    root["phase"] = picojson::value(phaseName(model.phase));
    root["selectedAnalysisId"] = picojson::value(model.selectedAnalysisId);
    root["selectedMethodId"] = picojson::value(model.selectedMethodId);
    root["selectedMetricId"] = picojson::value(model.selectedMetricId);
    root["selectedYear"] = number(model.selectedYear);
    root["activeArtifactId"] = picojson::value(model.activeArtifactId);
    root["availableArtifactIds"] = picojson::value(
        stringArray(model.availableArtifactIds));
    root["minimizedArtifactIds"] = picojson::value(
        stringArray(model.minimizedArtifactIds));
    root["errorCode"] = picojson::value(model.errorCode);
    root["errorMessage"] = picojson::value(model.errorMessage);

    picojson::object query;
    query["sourceId"] = picojson::value(model.draft.sourceId);
    query["geometry"] = picojson::value(geometryObject(model.draft.geometry));
    picojson::array years;
    for (int year : model.draft.time.explicitYears)
        years.push_back(number(year));
    query["years"] = picojson::value(years);
    root["draft"] = picojson::value(query);

    picojson::object target;
    target["locked"] = picojson::value(model.target.locked);
    target["requested"] = picojson::value(
        geometryObject(model.target.requested));
    target["requestedCenter"] = picojson::value(
        pointObject(model.target.requestedCenter));
    target["hasActualCoverage"] = picojson::value(
        model.target.hasActualCoverage);
    if (model.target.hasActualCoverage)
    {
        target["actualCoverage"] = picojson::value(
            geometryObject(model.target.actualCoverage));
        target["returnedCenter"] = picojson::value(
            pointObject(model.target.returnedCenter));
    }
    root["target"] = picojson::value(target);
    root["cost"] = picojson::value(costObject(model.cost));
    root["progress"] = picojson::value(progressObject(model.progress));

    picojson::array sourceValues;
    sourceValues.reserve(sources.size());
    for (const earthscience::ScienceSourceDescriptor& source : sources)
        sourceValues.push_back(picojson::value(sourceObject(source)));
    root["sources"] = picojson::value(sourceValues);
    root["activeArtifact"] = activeArtifact
        ? picojson::value(artifactObject(*activeArtifact))
        : picojson::value();
    root["reportEvidence"] = picojson::value(reportEvidenceObject(
        model, sources, activeArtifact));
    return picojson::value(root).serialize();
}

bool copyScienceWorkbenchSnapshot(
    const std::string& snapshot, OsgSolScienceUiBufferV1* output)
{
    if (!output || output->structSize < sizeof(OsgSolScienceUiBufferV1))
        return false;
    output->bytesWritten = 0;
    output->bytesRequired = snapshot.size() + 1;
    if (snapshot.size() > OSGSOL_SCIENCE_UI_SNAPSHOT_MAX_BYTES ||
        !validUtf8(snapshot.data(), snapshot.size()) || !output->utf8 ||
        output->capacity < output->bytesRequired)
        return false;
    std::memcpy(output->utf8, snapshot.data(), snapshot.size());
    output->utf8[snapshot.size()] = '\0';
    output->bytesWritten = snapshot.size();
    return true;
}

bool parseScienceWorkbenchAction(
    const char* utf8, std::size_t size, ScienceWorkbenchAction& action,
    std::string& error)
{
    error.clear();
    if (size > OSGSOL_SCIENCE_UI_ACTION_MAX_BYTES)
    {
        error = "workbench-action-too-large";
        return false;
    }
    if (!validUtf8(utf8, size))
    {
        error = "workbench-action-invalid-utf8";
        return false;
    }
    picojson::value parsed;
    const std::string parseError = picojson::parse(parsed, utf8, utf8 + size);
    if (!parseError.empty() || !parsed.is<picojson::object>())
    {
        error = "workbench-action-invalid-json";
        return false;
    }
    const picojson::object& object = parsed.get<picojson::object>();
    std::string schema;
    if (!getString(object, "schema", schema) ||
        schema != "science-workbench-action-v1")
    {
        error = "workbench-action-schema-incompatible";
        return false;
    }
    std::string name;
    if (!getString(object, "action", name))
    {
        error = "workbench-action-required";
        return false;
    }
    ScienceWorkbenchAction result;
    if (name == "select-source")
    {
        result.kind = ScienceWorkbenchActionKind::SelectSource;
        if (!getString(object, "sourceId", result.sourceId) ||
            result.sourceId.empty())
        {
            error = "workbench-source-required";
            return false;
        }
    }
    else if (name == "select-analysis")
    {
        result.kind = ScienceWorkbenchActionKind::SelectAnalysis;
        if (!getString(object, "analysisId", result.analysisId) ||
            result.analysisId.empty())
        {
            error = "workbench-analysis-required";
            return false;
        }
    }
    else if (name == "lock-map-center")
        result.kind = ScienceWorkbenchActionKind::LockMapCenter;
    else if (name == "lock-current-view")
        result.kind = ScienceWorkbenchActionKind::LockCurrentView;
    else if (name == "update-target")
    {
        result.kind = ScienceWorkbenchActionKind::UpdateLockedTarget;
        const auto found = object.find("geometry");
        if (found == object.end() ||
            !found->second.is<picojson::object>() ||
            !parseGeometry(found->second.get<picojson::object>(),
                           result.geometry))
        {
            error = "workbench-target-invalid";
            return false;
        }
    }
    else if (name == "set-year-range")
    {
        result.kind = ScienceWorkbenchActionKind::SetYearRange;
        if (!getInteger(object, "firstYear", result.firstYear) ||
            !getInteger(object, "lastYear", result.lastYear) ||
            result.firstYear <= 0 || result.lastYear <= 0 ||
            result.firstYear > result.lastYear ||
            result.lastYear - result.firstYear > 500)
        {
            error = "workbench-year-range-invalid";
            return false;
        }
    }
    else if (name == "set-method")
    {
        result.kind = ScienceWorkbenchActionKind::SetMethod;
        if (!getString(object, "methodId", result.methodId))
        {
            error = "workbench-method-invalid";
            return false;
        }
    }
    else if (name == "run")
        result.kind = ScienceWorkbenchActionKind::Run;
    else if (name == "cancel")
        result.kind = ScienceWorkbenchActionKind::Cancel;
    else if (name == "open-report")
    {
        result.kind = ScienceWorkbenchActionKind::OpenReport;
        if (!getString(object, "artifactId", result.artifactId))
        {
            error = "workbench-artifact-required";
            return false;
        }
    }
    else if (name == "minimize-report")
        result.kind = ScienceWorkbenchActionKind::MinimizeReport;
    else if (name == "close-report")
        result.kind = ScienceWorkbenchActionKind::CloseReport;
    else if (name == "remove-artifact")
    {
        result.kind = ScienceWorkbenchActionKind::RemoveArtifact;
        if (!getString(object, "artifactId", result.artifactId))
        {
            error = "workbench-artifact-required";
            return false;
        }
    }
    else if (name == "focus-target")
        result.kind = ScienceWorkbenchActionKind::FocusTarget;
    else if (name == "select-metric")
    {
        result.kind = ScienceWorkbenchActionKind::SelectMetric;
        if (!getString(object, "metricId", result.metricId))
        {
            error = "workbench-metric-required";
            return false;
        }
    }
    else if (name == "select-year")
    {
        result.kind = ScienceWorkbenchActionKind::SelectYear;
        if (!getInteger(object, "year", result.selectedYear) ||
            result.selectedYear <= 0)
        {
            error = "workbench-year-invalid";
            return false;
        }
    }
    else
    {
        error = "workbench-action-unknown";
        return false;
    }
    action = std::move(result);
    return true;
}
