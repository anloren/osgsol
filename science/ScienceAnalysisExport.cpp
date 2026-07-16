#include "ScienceAnalysisExport.h"

#include "ScienceAnalysisExportDetail.h"
#include "picojson.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace earthscience
{
namespace
{
    using analysisexportdetail::componentName;
    using analysisexportdetail::CSV_MISSING_VALUE;
    using analysisexportdetail::evidenceWarnings;
    using analysisexportdetail::evidenceYears;
    using analysisexportdetail::geometryKindName;
    using analysisexportdetail::sampleCount;
    using analysisexportdetail::SCHEMA_VERSION;
    using analysisexportdetail::sortedSourceIndices;
    using analysisexportdetail::upstreamDatasetIds;
    using analysisexportdetail::upstreamSourceIds;
    using analysisexportdetail::upstreamUrls;

    picojson::value numberOrNull(double value)
    {
        return std::isfinite(value)
            ? picojson::value(value) : picojson::value();
    }

    picojson::array stringsJson(const std::vector<std::string>& values)
    {
        picojson::array result;
        result.reserve(values.size());
        for (const std::string& value : values)
            result.emplace_back(value);
        return result;
    }

    picojson::array stringsJson(
        const std::shared_ptr<const std::vector<std::string>>& values)
    {
        return values ? stringsJson(*values) : picojson::array{};
    }

    template<typename T>
    picojson::array numbersJson(const std::vector<T>& values)
    {
        picojson::array result;
        result.reserve(values.size());
        for (const T& value : values)
            result.push_back(numberOrNull(static_cast<double>(value)));
        return result;
    }

    template<typename T>
    picojson::array numbersJson(
        const std::shared_ptr<const std::vector<T>>& values)
    {
        return values ? numbersJson(*values) : picojson::array{};
    }

    picojson::object boundsJson(const ScienceWgs84Bounds& bounds)
    {
        picojson::object result;
        result["east"] = numberOrNull(bounds.east);
        result["north"] = numberOrNull(bounds.north);
        result["south"] = numberOrNull(bounds.south);
        result["west"] = numberOrNull(bounds.west);
        return result;
    }

    picojson::object geometryJson(const ScienceArtifact& artifact)
    {
        const ScienceGeometry& geometry = artifact.query.geometry;
        picojson::object result;
        result["kind"] = picojson::value(geometryKindName(geometry.kind));
        result["requestedSpanMeters"] =
            numberOrNull(geometry.requestedSpanMeters);
        picojson::object point;
        point["latitude"] = numberOrNull(geometry.point.latitude);
        point["longitude"] = numberOrNull(geometry.point.longitude);
        result["point"] = picojson::value(point);
        result["requestedBounds"] = picojson::value(
            boundsJson(geometry.bounds));
        result["embeddingBounds"] = picojson::value(
            boundsJson(artifact.embedding.bounds));
        return result;
    }

    picojson::array sourcesJson(const ScienceArtifact& artifact)
    {
        picojson::array sources;
        for (std::size_t index : sortedSourceIndices(artifact))
        {
            const ScienceSourceReference& source =
                artifact.sourceReferences[index];
            picojson::object item;
            item["actualCoverage"] = picojson::value(
                boundsJson(source.actualCoverage));
            item["attribution"] = picojson::value(source.attribution);
            item["datasetId"] = picojson::value(source.datasetId);
            item["originalUrl"] = picojson::value(source.originalUrl);
            item["processingSteps"] = picojson::value(
                stringsJson(source.processingSteps));
            item["providerVersion"] =
                picojson::value(source.providerVersion);
            item["sourceId"] = picojson::value(source.sourceId);
            item["units"] = picojson::value(stringsJson(source.units));
            item["variables"] = picojson::value(
                stringsJson(source.variables));
            sources.emplace_back(item);
        }
        return sources;
    }

    picojson::object processingStepsJson(const ScienceArtifact& artifact)
    {
        picojson::object steps;
        steps["embedding"] = picojson::value(
            stringsJson(artifact.embedding.processingSteps));
        picojson::array sourceSteps;
        for (std::size_t index : sortedSourceIndices(artifact))
        {
            const ScienceSourceReference& source =
                artifact.sourceReferences[index];
            picojson::object item;
            item["datasetId"] = picojson::value(source.datasetId);
            item["sourceId"] = picojson::value(source.sourceId);
            item["steps"] = picojson::value(
                stringsJson(source.processingSteps));
            sourceSteps.emplace_back(item);
        }
        steps["sources"] = picojson::value(sourceSteps);
        return steps;
    }

    picojson::array matrixRowsJson(
        const std::shared_ptr<const std::vector<float>>& values,
        std::size_t rowCount, std::size_t columnCount)
    {
        picojson::array rows;
        if (!values || values->size() != rowCount * columnCount) return rows;
        rows.reserve(rowCount);
        for (std::size_t row = 0; row < rowCount; ++row)
        {
            picojson::array columns;
            columns.reserve(columnCount);
            for (std::size_t column = 0; column < columnCount; ++column)
            {
                columns.push_back(numberOrNull(
                    values->at(row * columnCount + column)));
            }
            rows.emplace_back(columns);
        }
        return rows;
    }

    picojson::array algorithmsJson(const ScienceArtifact& artifact)
    {
        picojson::array algorithms;
        const SciencePcaResult& pca = artifact.analysis.pca;
        if (pca.componentCount > 0 || pca.eigenvalues || pca.components)
        {
            picojson::object parameters;
            parameters["centeredPerComponent"] = picojson::value(true);
            parameters["componentCount"] =
                picojson::value(static_cast<double>(pca.componentCount));
            parameters["eigenpairOrder"] = picojson::value("descending");
            parameters["inputComponentCount"] =
                picojson::value(static_cast<double>(pca.inputComponentCount));
            parameters["signConvention"] = picojson::value(
                "largest-absolute-loading-positive; ties use lowest component index");
            parameters["standardized"] = picojson::value(false);
            parameters["validSamplesOnly"] = picojson::value(true);

            picojson::object result;
            result["eigenvalues"] = picojson::value(
                numbersJson(pca.eigenvalues));
            result["explainedVarianceRatios"] = picojson::value(
                numbersJson(pca.explainedVarianceRatios));
            result["loadings"] = picojson::value(matrixRowsJson(
                pca.components,
                static_cast<std::size_t>(std::max(0, pca.componentCount)),
                ScienceEmbeddingPayload::componentCount));
            result["scores"] = picojson::value(pca.scores
                ? numbersJson(*pca.scores) : picojson::array{});

            picojson::object algorithm;
            algorithm["implementation"] =
                picojson::value("Eigen::SelfAdjointEigenSolver<float,64x64>");
            algorithm["name"] = picojson::value("centered-pca");
            algorithm["parameters"] = picojson::value(parameters);
            algorithm["result"] = picojson::value(result);
            algorithms.emplace_back(algorithm);
        }

        const ScienceClusterResult& clusters = artifact.analysis.clusters;
        if (clusters.clusterCount > 0 || clusters.centroids ||
            clusters.assignments)
        {
            picojson::object parameters;
            parameters["assignment"] =
                picojson::value("double-precision cosine");
            parameters["emptyClusterHandling"] = picojson::value(
                "farthest assigned sample from a donor with population greater than one");
            parameters["finalIdOrder"] =
                picojson::value("lexicographic centroid order");
            parameters["initialization"] = picojson::value(
                "deterministic farthest-first in input traversal order");
            parameters["k"] = picojson::value(
                static_cast<double>(clusters.clusterCount));
            parameters["maxIterations"] = picojson::value(100.0);
            parameters["normalizedDirections"] = picojson::value(true);
            parameters["tolerance"] = picojson::value(1.0e-6);

            picojson::object result;
            result["assignments"] = picojson::value(
                numbersJson(clusters.assignments));
            result["centroids"] = picojson::value(matrixRowsJson(
                clusters.centroids,
                static_cast<std::size_t>(
                    std::max(0, clusters.clusterCount)),
                ScienceEmbeddingPayload::componentCount));
            result["concentrations"] = picojson::value(
                numbersJson(clusters.concentrations));
            result["converged"] = picojson::value(clusters.converged);
            result["iterations"] = picojson::value(
                static_cast<double>(clusters.iterations));
            result["populations"] = picojson::value(
                numbersJson(clusters.populations));

            picojson::object algorithm;
            algorithm["implementation"] =
                picojson::value("deterministic spherical k-means");
            algorithm["name"] = picojson::value("spherical-k-means");
            algorithm["parameters"] = picojson::value(parameters);
            algorithm["result"] = picojson::value(result);
            algorithms.emplace_back(algorithm);
        }
        return algorithms;
    }

    picojson::array samplesJson(
        const ScienceArtifact& artifact,
        const ScienceExportOptions& options,
        const std::vector<int>& years)
    {
        picojson::array samples;
        const std::size_t count = sampleCount(artifact, years);
        if (count == 0) return samples;
        const std::size_t width =
            static_cast<std::size_t>(artifact.embedding.width);
        const std::size_t height =
            static_cast<std::size_t>(artifact.embedding.height);
        const bool maskShape = artifact.embedding.mask &&
            artifact.embedding.mask->size() == count;
        const bool rawShape = artifact.embedding.values &&
            artifact.embedding.values->size() ==
                count * ScienceEmbeddingPayload::componentCount;
        const SciencePcaResult& pca = artifact.analysis.pca;
        const bool scoreShape = pca.scores && pca.componentCount > 0 &&
            pca.scores->size() ==
                count * static_cast<std::size_t>(pca.componentCount);
        const bool assignmentShape = artifact.analysis.clusters.assignments &&
            artifact.analysis.clusters.assignments->size() == count;
        samples.reserve(count);
        for (std::size_t sample = 0; sample < count; ++sample)
        {
            const std::size_t cell = sample % (width * height);
            const bool valid = !maskShape ||
                artifact.embedding.mask->at(sample) != 0;
            picojson::object item;
            item["sampleIndex"] = picojson::value(
                static_cast<double>(sample));
            item["valid"] = picojson::value(valid);
            item["x"] = picojson::value(
                static_cast<double>(cell % width));
            item["y"] = picojson::value(
                static_cast<double>(cell / width));
            item["year"] = picojson::value(
                static_cast<double>(years[sample / (width * height)]));
            if (scoreShape)
            {
                picojson::array scores;
                for (int component = 0;
                     component < pca.componentCount; ++component)
                {
                    scores.push_back(valid ? numberOrNull(pca.scores->at(
                        sample * static_cast<std::size_t>(pca.componentCount) +
                        static_cast<std::size_t>(component))) :
                        picojson::value());
                }
                item["pcaScores"] = picojson::value(scores);
            }
            if (assignmentShape)
            {
                const int assignment =
                    artifact.analysis.clusters.assignments->at(sample);
                item["clusterId"] = valid && assignment >= 0
                    ? picojson::value(static_cast<double>(assignment))
                    : picojson::value();
            }
            if (options.includeRawComponents)
            {
                picojson::object raw;
                for (int component = 0;
                     component < ScienceEmbeddingPayload::componentCount;
                     ++component)
                {
                    raw[componentName(component + 1)] = valid && rawShape
                        ? numberOrNull(artifact.embedding.values->at(
                            sample * ScienceEmbeddingPayload::componentCount +
                            static_cast<std::size_t>(component)))
                        : picojson::value();
                }
                item["embeddingComponents"] = picojson::value(raw);
            }
            samples.emplace_back(item);
        }
        return samples;
    }
}

std::string exportAnalysisJson(
    const ScienceArtifact& artifact, const ScienceExportOptions& options)
{
    const std::vector<int> years = evidenceYears(artifact);
    picojson::object root;
    root["algorithms"] = picojson::value(algorithmsJson(artifact));
    root["artifactId"] = picojson::value(artifact.artifactId);
    root["createdAt"] = picojson::value(artifact.createdAt);
    root["generation"] = picojson::value(
        std::to_string(artifact.generation));
    root["geometry"] = picojson::value(geometryJson(artifact));
    root["limitations"] = picojson::value(
        stringsJson(artifact.analysis.limitations));
    picojson::object missing;
    missing["csv"] = picojson::value(CSV_MISSING_VALUE);
    missing["json"] = picojson::value("null");
    missing["nonFiniteNumeric"] = picojson::value("null");
    root["missingValueRepresentation"] = picojson::value(missing);
    root["processingSteps"] = picojson::value(
        processingStepsJson(artifact));
    root["processingVersion"] =
        picojson::value(artifact.processingVersion);
    root["samples"] = picojson::value(
        samplesJson(artifact, options, years));
    root["schemaVersion"] = picojson::value(SCHEMA_VERSION);
    root["sources"] = picojson::value(sourcesJson(artifact));
    picojson::object upstream;
    upstream["artifactId"] = picojson::value(artifact.artifactId);
    upstream["datasetIds"] = picojson::value(
        stringsJson(upstreamDatasetIds(artifact)));
    upstream["originalUrls"] = picojson::value(
        stringsJson(upstreamUrls(artifact)));
    upstream["sourceIds"] = picojson::value(
        stringsJson(upstreamSourceIds(artifact)));
    root["upstreamIds"] = picojson::value(upstream);
    root["warnings"] = picojson::value(
        stringsJson(evidenceWarnings(artifact)));
    root["years"] = picojson::value(numbersJson(years));
    return picojson::value(root).serialize(false);
}
}
