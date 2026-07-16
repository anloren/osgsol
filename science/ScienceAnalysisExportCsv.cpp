#include "ScienceAnalysisExport.h"

#include "ScienceAnalysisExportDetail.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
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
    using analysisexportdetail::providerVersions;
    using analysisexportdetail::sampleCount;
    using analysisexportdetail::SCHEMA_VERSION;
    using analysisexportdetail::sortedSourceIndices;
    using analysisexportdetail::sortedUnique;
    using analysisexportdetail::upstreamDatasetIds;
    using analysisexportdetail::upstreamSourceIds;
    using analysisexportdetail::upstreamUrls;

    std::string numericText(double value)
    {
        if (!std::isfinite(value)) return CSV_MISSING_VALUE;
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(
            std::numeric_limits<double>::max_digits10) << value;
        return stream.str();
    }

    std::string csvEscape(const std::string& field)
    {
        if (field.find_first_of(",\"\r\n") == std::string::npos)
            return field;
        std::string escaped;
        escaped.reserve(field.size() + 2);
        escaped.push_back('"');
        for (char character : field)
        {
            if (character == '"') escaped.push_back('"');
            escaped.push_back(character);
        }
        escaped.push_back('"');
        return escaped;
    }

    std::string join(const std::vector<std::string>& values,
                     const std::string& separator)
    {
        std::string result;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            if (index != 0) result += separator;
            result += values[index];
        }
        return result;
    }

    template<typename T>
    std::string numericVectorText(
        const std::shared_ptr<const std::vector<T>>& values)
    {
        if (!values) return CSV_MISSING_VALUE;
        std::vector<std::string> fields;
        fields.reserve(values->size());
        for (const T& value : *values)
            fields.push_back(numericText(static_cast<double>(value)));
        return join(fields, "|");
    }

    std::string numericMatrixText(
        const std::shared_ptr<const std::vector<float>>& values,
        std::size_t rows, std::size_t columns)
    {
        if (!values || values->size() != rows * columns)
            return CSV_MISSING_VALUE;
        std::vector<std::string> rowFields;
        rowFields.reserve(rows);
        for (std::size_t row = 0; row < rows; ++row)
        {
            std::vector<std::string> columnsText;
            columnsText.reserve(columns);
            for (std::size_t column = 0; column < columns; ++column)
            {
                columnsText.push_back(numericText(
                    values->at(row * columns + column)));
            }
            rowFields.push_back(join(columnsText, ";"));
        }
        return join(rowFields, "|");
    }

    std::vector<std::string> flattenedProcessingSteps(
        const ScienceArtifact& artifact)
    {
        std::vector<std::string> steps;
        if (artifact.embedding.processingSteps)
        {
            steps.insert(steps.end(),
                         artifact.embedding.processingSteps->begin(),
                         artifact.embedding.processingSteps->end());
        }
        for (std::size_t index : sortedSourceIndices(artifact))
        {
            const std::vector<std::string>& sourceSteps =
                artifact.sourceReferences[index].processingSteps;
            steps.insert(steps.end(), sourceSteps.begin(), sourceSteps.end());
        }
        return steps;
    }

    std::string geometryCsv(const ScienceArtifact& artifact)
    {
        const ScienceWgs84Bounds& bounds = artifact.query.geometry.bounds;
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(
                   std::numeric_limits<double>::max_digits10)
               << "west=" << bounds.west << ";south=" << bounds.south
               << ";east=" << bounds.east << ";north=" << bounds.north
               << ";latitude=" << artifact.query.geometry.point.latitude
               << ";longitude=" << artifact.query.geometry.point.longitude;
        return stream.str();
    }

    std::vector<std::string> algorithmNames(
        const ScienceArtifact& artifact)
    {
        std::vector<std::string> names;
        if (artifact.analysis.pca.componentCount > 0 ||
            artifact.analysis.pca.eigenvalues)
            names.push_back("centered-pca");
        if (artifact.analysis.clusters.clusterCount > 0 ||
            artifact.analysis.clusters.assignments)
            names.push_back("spherical-k-means");
        return names;
    }

    std::string algorithmParametersCsv(const ScienceArtifact& artifact)
    {
        std::vector<std::string> parameters;
        if (artifact.analysis.pca.componentCount > 0 ||
            artifact.analysis.pca.eigenvalues)
        {
            parameters.push_back(
                "pca:centered-per-component=true;standardized=false;"
                "solver=Eigen-SelfAdjointEigenSolver;order=descending;"
                "sign=largest-absolute-positive-tie-lowest-index;components=" +
                std::to_string(artifact.analysis.pca.componentCount));
        }
        if (artifact.analysis.clusters.clusterCount > 0 ||
            artifact.analysis.clusters.assignments)
        {
            parameters.push_back(
                "spherical-k-means:k=" +
                std::to_string(artifact.analysis.clusters.clusterCount) +
                ";init=deterministic-farthest-first;cosine=double;"
                "tolerance=1e-6;max-iterations=100;"
                "final-order=lexicographic-centroid");
        }
        return join(parameters, "|");
    }

    void appendCsvRow(std::ostringstream& stream,
                      const std::vector<std::string>& fields)
    {
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            if (index != 0) stream << ',';
            stream << csvEscape(fields[index]);
        }
        stream << '\n';
    }
}

std::string exportAnalysisCsv(
    const ScienceArtifact& artifact, const ScienceExportOptions& options)
{
    const std::vector<int> years = evidenceYears(artifact);
    const std::size_t count = sampleCount(artifact, years);
    const std::size_t width = artifact.embedding.width > 0
        ? static_cast<std::size_t>(artifact.embedding.width) : 0;
    const std::size_t height = artifact.embedding.height > 0
        ? static_cast<std::size_t>(artifact.embedding.height) : 0;
    const std::size_t cells = width * height;
    const SciencePcaResult& pca = artifact.analysis.pca;
    const bool scoreShape = pca.scores && pca.componentCount > 0 &&
        pca.scores->size() ==
            count * static_cast<std::size_t>(pca.componentCount);
    const bool assignmentShape = artifact.analysis.clusters.assignments &&
        artifact.analysis.clusters.assignments->size() == count;
    const bool rawShape = artifact.embedding.values &&
        artifact.embedding.values->size() ==
            count * ScienceEmbeddingPayload::componentCount;
    const bool maskShape = artifact.embedding.mask &&
        artifact.embedding.mask->size() == count;

    std::vector<std::string> header = {
        "schema_version", "artifact_id", "generation", "geometry_kind",
        "geometry", "year", "x", "y", "valid", "source_ids",
        "dataset_ids", "provider_versions", "processing_version",
        "processing_steps", "algorithms", "parameters", "warnings",
        "limitations", "upstream_ids", "missing_representation",
        "pca_eigenvalues", "pca_explained_variance_ratios",
        "pca_loadings", "cluster_centroids", "cluster_populations",
        "cluster_concentrations", "cluster_converged",
        "cluster_iterations", "cluster_id"};
    for (int component = 0; component < pca.componentCount; ++component)
        header.push_back("PC" + std::to_string(component + 1));
    if (options.includeRawComponents)
    {
        for (int component = 1;
             component <= ScienceEmbeddingPayload::componentCount;
             ++component)
            header.push_back(componentName(component));
    }

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    appendCsvRow(stream, header);
    const std::string sourceIds = join(upstreamSourceIds(artifact), "|");
    const std::string datasetIds = join(upstreamDatasetIds(artifact), "|");
    const std::string providers = join(providerVersions(artifact), "|");
    const std::string processing =
        join(flattenedProcessingSteps(artifact), "|");
    const std::string algorithms = join(algorithmNames(artifact), "|");
    const std::string parameters = algorithmParametersCsv(artifact);
    const std::string warnings = join(evidenceWarnings(artifact), "|");
    const std::string limitations = artifact.analysis.limitations
        ? join(*artifact.analysis.limitations, "|") : std::string();
    std::vector<std::string> upstream = upstreamSourceIds(artifact);
    const std::vector<std::string> datasets = upstreamDatasetIds(artifact);
    const std::vector<std::string> urls = upstreamUrls(artifact);
    upstream.insert(upstream.end(), datasets.begin(), datasets.end());
    upstream.insert(upstream.end(), urls.begin(), urls.end());
    if (!artifact.artifactId.empty()) upstream.push_back(artifact.artifactId);
    const std::string upstreamIds =
        join(sortedUnique(std::move(upstream)), "|");

    for (std::size_t sample = 0; sample < count; ++sample)
    {
        const bool valid = !maskShape ||
            artifact.embedding.mask->at(sample) != 0;
        const std::size_t cell = cells == 0 ? 0 : sample % cells;
        std::vector<std::string> fields = {
            SCHEMA_VERSION,
            artifact.artifactId,
            std::to_string(artifact.generation),
            geometryKindName(artifact.query.geometry.kind),
            geometryCsv(artifact),
            cells == 0 ? CSV_MISSING_VALUE :
                std::to_string(years[sample / cells]),
            width == 0 ? CSV_MISSING_VALUE :
                std::to_string(cell % width),
            width == 0 ? CSV_MISSING_VALUE :
                std::to_string(cell / width),
            valid ? "1" : "0",
            sourceIds,
            datasetIds,
            providers,
            artifact.processingVersion,
            processing,
            algorithms,
            parameters,
            warnings,
            limitations,
            upstreamIds,
            CSV_MISSING_VALUE,
            numericVectorText(pca.eigenvalues),
            numericVectorText(pca.explainedVarianceRatios),
            numericMatrixText(
                pca.components,
                static_cast<std::size_t>(std::max(0, pca.componentCount)),
                ScienceEmbeddingPayload::componentCount),
            numericMatrixText(
                artifact.analysis.clusters.centroids,
                static_cast<std::size_t>(std::max(
                    0, artifact.analysis.clusters.clusterCount)),
                ScienceEmbeddingPayload::componentCount),
            numericVectorText(artifact.analysis.clusters.populations),
            numericVectorText(artifact.analysis.clusters.concentrations),
            artifact.analysis.clusters.converged ? "true" : "false",
            std::to_string(artifact.analysis.clusters.iterations),
            valid && assignmentShape &&
                artifact.analysis.clusters.assignments->at(sample) >= 0
                ? std::to_string(
                    artifact.analysis.clusters.assignments->at(sample))
                : CSV_MISSING_VALUE};
        for (int component = 0; component < pca.componentCount; ++component)
        {
            fields.push_back(valid && scoreShape
                ? numericText(pca.scores->at(
                    sample * static_cast<std::size_t>(pca.componentCount) +
                    static_cast<std::size_t>(component)))
                : CSV_MISSING_VALUE);
        }
        if (options.includeRawComponents)
        {
            for (int component = 0;
                 component < ScienceEmbeddingPayload::componentCount;
                 ++component)
            {
                fields.push_back(valid && rawShape
                    ? numericText(artifact.embedding.values->at(
                        sample * ScienceEmbeddingPayload::componentCount +
                        static_cast<std::size_t>(component)))
                    : CSV_MISSING_VALUE);
            }
        }
        appendCsvRow(stream, fields);
    }
    return stream.str();
}
}
