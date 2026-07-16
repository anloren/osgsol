#include "ScienceAnalysisExport.h"

#include "picojson.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    earthscience::ScienceArtifact makeEvidenceFixture()
    {
        earthscience::ScienceArtifact artifact;
        artifact.artifactId = "upstream-\"artifact\",7";
        artifact.generation = 7;
        artifact.processingVersion = "science-core-2.0";
        artifact.createdAt = "2026-07-16T08:00:00Z";
        artifact.query.sourceId = "alpha,earth";
        artifact.query.geometry.kind =
            earthscience::ScienceGeometryKind::BoundingBox;
        artifact.query.geometry.bounds = {100.25, 20.5, 101.25, 21.5};
        artifact.query.time.mode =
            earthscience::ScienceTimeMode::ExplicitYears;
        artifact.query.time.explicitYears = {2020, 2021};
        artifact.query.variables = {"A01-A64"};
        artifact.query.analysis.pcaComponents = 2;
        artifact.query.analysis.clusterCount = 2;
        artifact.warnings = {"coverage, partial", "quote: \"check\""};

        earthscience::ScienceSourceReference source;
        source.sourceId = "alpha,earth";
        source.datasetId = "dataset\nA-01";
        source.providerVersion = "preview-\"v1\"";
        source.originalUrl = "upstream://alpha/collection";
        source.processingSteps = {"dequantize", "align, center"};
        artifact.sourceReferences.push_back(source);

        artifact.embedding.years =
            std::make_shared<const std::vector<int>>(
                std::initializer_list<int>{2020, 2021});
        artifact.embedding.width = 2;
        artifact.embedding.height = 1;
        std::vector<float> values(
            4 * earthscience::ScienceEmbeddingPayload::componentCount, 0.0f);
        for (std::size_t sample = 0; sample < 4; ++sample)
        {
            const std::size_t offset = sample * 64;
            values[offset] = static_cast<float>(sample + 1);
            values[offset + 1] = static_cast<float>(sample) * 0.25f;
        }
        artifact.embedding.values =
            std::make_shared<const std::vector<float>>(std::move(values));
        artifact.embedding.mask =
            std::make_shared<const std::vector<unsigned char>>(
                std::initializer_list<unsigned char>{1, 0, 1, 1});
        artifact.embedding.bounds = artifact.query.geometry.bounds;
        artifact.embedding.actualResolutionMeters = 30.0;
        artifact.embedding.processingSteps =
            std::make_shared<const std::vector<std::string>>(
                std::initializer_list<std::string>{
                    "dequantize int8 with stored scale", "center for PCA"});
        artifact.embedding.warnings =
            std::make_shared<const std::vector<std::string>>(
                1, "masked sample retained as missing");

        artifact.analysis.pca.inputComponentCount = 64;
        artifact.analysis.pca.componentCount = 2;
        artifact.analysis.pca.eigenvalues =
            std::make_shared<const std::vector<double>>(
                std::initializer_list<double>{3.0, 1.0});
        artifact.analysis.pca.explainedVarianceRatios =
            std::make_shared<const std::vector<double>>(
                std::initializer_list<double>{0.75, 0.25});
        std::vector<float> components(128, 0.0f);
        components[0] = 1.0f;
        components[65] = 1.0f;
        artifact.analysis.pca.components =
            std::make_shared<const std::vector<float>>(
                std::move(components));
        artifact.analysis.pca.scores =
            std::make_shared<const std::vector<float>>(
                std::initializer_list<float>{
                    -1.0f, 0.0f,
                    std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::quiet_NaN(),
                    0.5f, -0.5f, 1.0f, 0.5f});

        artifact.analysis.clusters.metric =
            earthscience::ScienceMetric::CosineDistance;
        artifact.analysis.clusters.clusterCount = 2;
        artifact.analysis.clusters.assignments =
            std::make_shared<const std::vector<int>>(
                std::initializer_list<int>{0, -1, 0, 1});
        artifact.analysis.clusters.centroids =
            std::make_shared<const std::vector<float>>(128, 0.0f);
        artifact.analysis.clusters.populations =
            std::make_shared<const std::vector<std::uint64_t>>(
                std::initializer_list<std::uint64_t>{2, 1});
        artifact.analysis.clusters.concentrations =
            std::make_shared<const std::vector<double>>(
                std::initializer_list<double>{0.98, 1.0});
        artifact.analysis.clusters.converged = true;
        artifact.analysis.clusters.iterations = 4;
        artifact.analysis.limitations =
            std::make_shared<const std::vector<std::string>>(
                std::initializer_list<std::string>{
                    "PCA axes are local mathematical structure only.",
                    "Clusters have no physical class labels."});
        return artifact;
    }

    std::string firstLine(const std::string& text)
    {
        const std::size_t end = text.find('\n');
        return text.substr(0, end);
    }

    std::string componentName(int component)
    {
        std::ostringstream stream;
        stream << 'A';
        if (component < 10) stream << '0';
        stream << component;
        return stream.str();
    }

    std::string replayHash(const std::string& serialized)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char byte : serialized)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        std::ostringstream stream;
        stream << std::hex << std::setfill('0') << std::setw(16) << hash;
        return stream.str();
    }

    class CommaNumpunct : public std::numpunct<char>
    {
    protected:
        char do_decimal_point() const override { return ','; }
    };

    void testJsonIsParseableCompleteEscapedAndDeterministic()
    {
        const earthscience::ScienceArtifact artifact = makeEvidenceFixture();
        const std::locale previous = std::locale();
        std::locale::global(std::locale(
            std::locale::classic(), new CommaNumpunct));
        const std::string first = earthscience::exportAnalysisJson(
            artifact, earthscience::ScienceExportOptions{});
        const std::string second = earthscience::exportAnalysisJson(
            artifact, earthscience::ScienceExportOptions{});
        std::locale::global(previous);

        require(first == second,
                "JSON evidence changed on deterministic replay");
        std::cout << "[REPLAY] evidence JSON FNV-1a "
                  << replayHash(first) << '\n';
        require(first.find("100.25") != std::string::npos,
                "JSON numeric formatting followed the process locale");
        picojson::value document;
        const std::string parseError = picojson::parse(document, first);
        require(parseError.empty() && document.is<picojson::object>(),
                "JSON evidence is not parseable");
        const picojson::object& root = document.get<picojson::object>();
        require(root.count("geometry") == 1 &&
                    root.at("geometry").is<picojson::object>() &&
                    root.count("years") == 1 &&
                    root.at("years").is<picojson::array>() &&
                    root.at("years").get<picojson::array>().size() == 2 &&
                    root.count("sources") == 1 &&
                    root.at("sources").is<picojson::array>() &&
                    root.count("processingVersion") == 1 &&
                    root.count("processingSteps") == 1 &&
                    root.count("algorithms") == 1 &&
                    root.at("algorithms").is<picojson::array>() &&
                    root.at("algorithms").get<picojson::array>().size() == 2 &&
                    root.count("warnings") == 1 &&
                    root.count("limitations") == 1 &&
                    root.count("upstreamIds") == 1,
                "JSON evidence omitted provenance or limitations");
        for (const picojson::value& algorithm :
             root.at("algorithms").get<picojson::array>())
        {
            require(algorithm.is<picojson::object>() &&
                        algorithm.get<picojson::object>().count(
                            "parameters") == 1,
                    "JSON algorithm omitted reproducibility parameters");
        }
        const picojson::object& source = root.at("sources")
            .get<picojson::array>().front().get<picojson::object>();
        require(source.at("datasetId").get<std::string>() ==
                    "dataset\nA-01" &&
                    source.at("providerVersion").get<std::string>() ==
                        "preview-\"v1\"",
                "JSON escaping changed source provenance");
        require(first.find("\"A01\"") == std::string::npos &&
                    first.find("rgba") == std::string::npos,
                "default JSON leaked raw components or RGB display bytes");
    }

    void testCsvEscapesFieldsAndRawComponentsAreExplicitOptIn()
    {
        const earthscience::ScienceArtifact artifact = makeEvidenceFixture();
        const std::string defaultCsv = earthscience::exportAnalysisCsv(
            artifact, earthscience::ScienceExportOptions{});
        const std::string replay = earthscience::exportAnalysisCsv(
            artifact, earthscience::ScienceExportOptions{});
        require(defaultCsv == replay,
                "CSV evidence changed on deterministic replay");
        std::cout << "[REPLAY] evidence CSV FNV-1a "
                  << replayHash(defaultCsv) << '\n';
        require(firstLine(defaultCsv).find("A01") == std::string::npos &&
                    firstLine(defaultCsv).find("A64") == std::string::npos,
                "default CSV included raw embedding components");
        require(defaultCsv.find("\"alpha,earth\"") != std::string::npos &&
                    defaultCsv.find("\"dataset\nA-01\"") !=
                        std::string::npos &&
                    defaultCsv.find("NA") != std::string::npos,
                "CSV escaping or explicit missing representation changed");
        require(defaultCsv.find("rgba") == std::string::npos,
                "CSV exported RGB display bytes as embeddings");
        require(firstLine(defaultCsv).find("pca_eigenvalues") !=
                    std::string::npos &&
                    firstLine(defaultCsv).find("pca_loadings") !=
                        std::string::npos &&
                    firstLine(defaultCsv).find("cluster_populations") !=
                        std::string::npos &&
                    firstLine(defaultCsv).find("cluster_concentrations") !=
                        std::string::npos &&
                    firstLine(defaultCsv).find("cluster_converged") !=
                        std::string::npos,
                "CSV evidence omitted aggregate latent diagnostics");

        earthscience::ScienceExportOptions rawOptions;
        rawOptions.includeRawComponents = true;
        const std::string rawCsv =
            earthscience::exportAnalysisCsv(artifact, rawOptions);
        const std::string rawJson =
            earthscience::exportAnalysisJson(artifact, rawOptions);
        const std::string rawHeader = firstLine(rawCsv);
        for (int component = 1; component <= 64; ++component)
        {
            const std::string name = componentName(component);
            require(rawHeader.find(name) != std::string::npos,
                    "raw CSV did not contain all 64 components");
            require(rawJson.find("\"" + name + "\"") !=
                        std::string::npos,
                    "raw JSON did not contain all 64 components");
        }
    }
}

int main()
{
    testJsonIsParseableCompleteEscapedAndDeterministic();
    testCsvEscapesFieldsAndRawComponentsAreExplicitOptIn();
    std::cout << "Science analysis export tests passed\n";
    return 0;
}
