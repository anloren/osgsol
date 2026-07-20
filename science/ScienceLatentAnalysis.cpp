#include "ScienceAnalysisEngine.h"

#include "ScienceAnalysisSupport.h"

#include <Eigen/Eigenvalues>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace earthscience
{
namespace
{
    constexpr int COMPONENT_COUNT = ScienceEmbeddingPayload::componentCount;
    constexpr double CLUSTER_TOLERANCE = 1.0e-6;
    constexpr int MAX_CLUSTER_ITERATIONS = 100;

    bool fail(std::string& error, const std::string& message)
    {
        error = message;
        return false;
    }

    bool isCancelled(const std::function<bool()>& cancelled)
    {
        return cancelled && cancelled();
    }

    bool collectValidSamples(
        const ScienceEmbeddingPayload& embedding,
        std::vector<std::size_t>& sampleIndices,
        std::size_t& sampleCount, std::string& error,
        const std::string& context)
    {
        std::size_t cellCount = 0;
        if (!analysisdetail::validateEmbeddingShape(
                embedding, false, cellCount, error, context))
            return false;
        sampleCount = embedding.years->size() * cellCount;
        sampleIndices.clear();
        sampleIndices.reserve(sampleCount);
        for (std::size_t sample = 0; sample < sampleCount; ++sample)
        {
            if (embedding.mask->at(sample) != 0)
                sampleIndices.push_back(sample);
        }
        return true;
    }

    double dotDirection(const std::vector<double>& directions,
                        std::size_t directionIndex,
                        const std::vector<double>& centroid)
    {
        const std::size_t offset = directionIndex * COMPONENT_COUNT;
        double dot = 0.0;
        for (int component = 0; component < COMPONENT_COUNT; ++component)
        {
            dot += directions[offset + static_cast<std::size_t>(component)] *
                   centroid[static_cast<std::size_t>(component)];
        }
        return dot;
    }

    bool centroidLess(const std::vector<float>& centroids,
                      int left, int right)
    {
        const std::size_t leftOffset =
            static_cast<std::size_t>(left) * COMPONENT_COUNT;
        const std::size_t rightOffset =
            static_cast<std::size_t>(right) * COMPONENT_COUNT;
        for (int component = 0; component < COMPONENT_COUNT; ++component)
        {
            const std::size_t index = static_cast<std::size_t>(component);
            if (centroids[leftOffset + index] < centroids[rightOffset + index])
                return true;
            if (centroids[leftOffset + index] > centroids[rightOffset + index])
                return false;
        }
        return left < right;
    }
}

bool ScienceAnalysisEngine::computeLocalPca(
    const ScienceEmbeddingPayload& embedding, int componentCount,
    ScienceAnalysisPayload& output,
    const std::function<bool()>& cancelled, std::string& error)
{
    error.clear();
    if (componentCount < 1 || componentCount > COMPONENT_COUNT)
        return fail(error, "local PCA component count must be 1 through 64");
    if (isCancelled(cancelled))
        return fail(error, "local PCA was cancelled");

    std::vector<std::size_t> sampleIndices;
    std::size_t sampleCount = 0;
    if (!collectValidSamples(embedding, sampleIndices, sampleCount,
                             error, "local PCA"))
        return false;
    if (sampleIndices.size() < 2)
        return fail(error,
                    "local PCA requires at least two valid samples");
    if (sampleIndices.size() <= static_cast<std::size_t>(componentCount))
        return fail(error,
                    "local PCA requires more valid samples than requested components");

    Eigen::MatrixXf samples(
        static_cast<Eigen::Index>(sampleIndices.size()), COMPONENT_COUNT);
    for (std::size_t row = 0; row < sampleIndices.size(); ++row)
    {
        if ((row & 255u) == 0u && isCancelled(cancelled))
            return fail(error, "local PCA was cancelled");
        const float* values =
            analysisdetail::vectorAt(embedding, sampleIndices[row]);
        for (int component = 0; component < COMPONENT_COUNT; ++component)
        {
            const float value = values[component];
            if (!std::isfinite(value))
                return fail(error,
                            "local PCA requires finite valid samples");
            samples(static_cast<Eigen::Index>(row), component) = value;
        }
    }

    const Eigen::Matrix<float, 1, COMPONENT_COUNT> means =
        samples.colwise().mean();
    Eigen::MatrixXf centered = samples.rowwise() - means;
    Eigen::Matrix<float, COMPONENT_COUNT, COMPONENT_COUNT> covariance =
        (centered.transpose() * centered) /
        static_cast<float>(sampleIndices.size() - 1);
    if (!covariance.allFinite())
        return fail(error, "local PCA covariance is not finite");
    const double totalVariance = static_cast<double>(covariance.trace());
    if (!std::isfinite(totalVariance) || totalVariance <= 0.0)
        return fail(error, "local PCA samples have zero variance");
    if (isCancelled(cancelled))
        return fail(error, "local PCA was cancelled");

    Eigen::SelfAdjointEigenSolver<
        Eigen::Matrix<float, COMPONENT_COUNT, COMPONENT_COUNT>> solver(
            covariance);
    if (solver.info() != Eigen::Success ||
        !solver.eigenvalues().allFinite() ||
        !solver.eigenvectors().allFinite())
        return fail(error, "local PCA eigensolver failed");
    if (isCancelled(cancelled))
        return fail(error, "local PCA was cancelled");

    std::vector<double> eigenvalues;
    std::vector<double> ratios;
    std::vector<float> components;
    eigenvalues.reserve(static_cast<std::size_t>(componentCount));
    ratios.reserve(static_cast<std::size_t>(componentCount));
    components.reserve(
        static_cast<std::size_t>(componentCount) * COMPONENT_COUNT);
    for (int axis = 0; axis < componentCount; ++axis)
    {
        const int solverIndex = COMPONENT_COUNT - 1 - axis;
        const double eigenvalue = std::max(
            0.0, static_cast<double>(solver.eigenvalues()[solverIndex]));
        Eigen::Matrix<float, COMPONENT_COUNT, 1> loading =
            solver.eigenvectors().col(solverIndex);
        int largestIndex = 0;
        float largestAbsolute = std::abs(loading[0]);
        for (int component = 1; component < COMPONENT_COUNT; ++component)
        {
            const float absolute = std::abs(loading[component]);
            if (absolute > largestAbsolute)
            {
                largestAbsolute = absolute;
                largestIndex = component;
            }
        }
        if (loading[largestIndex] < 0.0f) loading = -loading;

        eigenvalues.push_back(eigenvalue);
        ratios.push_back(eigenvalue / totalVariance);
        for (int component = 0; component < COMPONENT_COUNT; ++component)
            components.push_back(loading[component]);
    }

    std::vector<float> scores(
        sampleCount * static_cast<std::size_t>(componentCount),
        std::numeric_limits<float>::quiet_NaN());
    for (std::size_t row = 0; row < sampleIndices.size(); ++row)
    {
        if ((row & 255u) == 0u && isCancelled(cancelled))
            return fail(error, "local PCA was cancelled");
        for (int axis = 0; axis < componentCount; ++axis)
        {
            float score = 0.0f;
            const std::size_t loadingOffset =
                static_cast<std::size_t>(axis) * COMPONENT_COUNT;
            for (int component = 0; component < COMPONENT_COUNT; ++component)
            {
                score += centered(static_cast<Eigen::Index>(row), component) *
                    components[loadingOffset +
                               static_cast<std::size_t>(component)];
            }
            scores[sampleIndices[row] *
                       static_cast<std::size_t>(componentCount) +
                   static_cast<std::size_t>(axis)] = score;
        }
    }
    if (isCancelled(cancelled))
        return fail(error, "local PCA was cancelled");

    ScienceAnalysisPayload candidate;
    candidate.kind = ScienceAnalysisKind::PrincipalComponents;
    candidate.pca.inputComponentCount = COMPONENT_COUNT;
    candidate.pca.componentCount = componentCount;
    candidate.pca.components =
        std::make_shared<const std::vector<float>>(std::move(components));
    candidate.pca.scores =
        std::make_shared<const std::vector<float>>(std::move(scores));
    candidate.pca.eigenvalues =
        std::make_shared<const std::vector<double>>(std::move(eigenvalues));
    candidate.pca.explainedVarianceRatios =
        std::make_shared<const std::vector<double>>(std::move(ratios));
    candidate.interpretation =
        std::make_shared<const std::vector<std::string>>(
            1, "PCA axes describe local mathematical structure in centered embedding space.");
    candidate.limitations =
        std::make_shared<const std::vector<std::string>>(
            std::initializer_list<std::string>{
                "PCA loadings have no assigned physical labels.",
                "Components were centered per dimension and were not standardized."});
    if (isCancelled(cancelled))
        return fail(error, "local PCA was cancelled");
    output = std::move(candidate);
    return true;
}

bool ScienceAnalysisEngine::computeSphericalClusters(
    const ScienceEmbeddingPayload& embedding, int clusterCount,
    ScienceAnalysisPayload& output,
    const std::function<bool()>& cancelled, std::string& error)
{
    error.clear();
    if (clusterCount < 2 || clusterCount > 8)
        return fail(error,
                    "spherical cluster count must be 2 through 8");
    if (isCancelled(cancelled))
        return fail(error, "spherical clustering was cancelled");

    std::vector<std::size_t> sampleIndices;
    std::size_t sampleCount = 0;
    if (!collectValidSamples(embedding, sampleIndices, sampleCount,
                             error, "spherical clustering"))
        return false;
    if (sampleIndices.size() < static_cast<std::size_t>(clusterCount))
        return fail(error,
                    "spherical clustering requires at least k valid samples");

    std::vector<double> directions(
        sampleIndices.size() * COMPONENT_COUNT, 0.0);
    for (std::size_t row = 0; row < sampleIndices.size(); ++row)
    {
        if ((row & 255u) == 0u && isCancelled(cancelled))
            return fail(error, "spherical clustering was cancelled");
        const float* values =
            analysisdetail::vectorAt(embedding, sampleIndices[row]);
        double squaredNorm = 0.0;
        const std::size_t offset = row * COMPONENT_COUNT;
        for (int component = 0; component < COMPONENT_COUNT; ++component)
        {
            const float value = values[component];
            if (!std::isfinite(value))
                return fail(error,
                            "spherical clustering requires finite valid samples");
            directions[offset + static_cast<std::size_t>(component)] = value;
            squaredNorm += static_cast<double>(value) * value;
        }
        if (!std::isfinite(squaredNorm) || squaredNorm <= 0.0)
            return fail(error,
                        "spherical clustering requires positive-norm valid samples");
        const double inverseNorm = 1.0 / std::sqrt(squaredNorm);
        for (int component = 0; component < COMPONENT_COUNT; ++component)
            directions[offset + static_cast<std::size_t>(component)] *=
                inverseNorm;
    }

    std::vector<std::vector<double>> centroids(
        static_cast<std::size_t>(clusterCount),
        std::vector<double>(COMPONENT_COUNT, 0.0));
    std::vector<bool> seeded(sampleIndices.size(), false);
    for (int component = 0; component < COMPONENT_COUNT; ++component)
        centroids[0][static_cast<std::size_t>(component)] =
            directions[static_cast<std::size_t>(component)];
    seeded[0] = true;
    for (int cluster = 1; cluster < clusterCount; ++cluster)
    {
        if (isCancelled(cancelled))
            return fail(error, "spherical clustering was cancelled");
        std::size_t selected = sampleIndices.size();
        double farthestDistance = -1.0;
        for (std::size_t row = 0; row < sampleIndices.size(); ++row)
        {
            if ((row & 255u) == 0u && isCancelled(cancelled))
                return fail(error, "spherical clustering was cancelled");
            if (seeded[row]) continue;
            double nearestCosine = -std::numeric_limits<double>::infinity();
            for (int existing = 0; existing < cluster; ++existing)
            {
                nearestCosine = std::max(
                    nearestCosine,
                    dotDirection(directions, row,
                                 centroids[static_cast<std::size_t>(existing)]));
            }
            const double distance = 1.0 - nearestCosine;
            if (distance > farthestDistance)
            {
                farthestDistance = distance;
                selected = row;
            }
        }
        if (selected == sampleIndices.size())
            return fail(error,
                        "spherical clustering initialization failed");
        seeded[selected] = true;
        const std::size_t offset = selected * COMPONENT_COUNT;
        for (int component = 0; component < COMPONENT_COUNT; ++component)
        {
            centroids[static_cast<std::size_t>(cluster)]
                     [static_cast<std::size_t>(component)] =
                directions[offset + static_cast<std::size_t>(component)];
        }
    }

    std::vector<int> assignments(sampleIndices.size(), -1);
    std::vector<int> previousAssignments;
    bool converged = false;
    int iterations = 0;
    for (int iteration = 1;
         iteration <= MAX_CLUSTER_ITERATIONS; ++iteration)
    {
        if (isCancelled(cancelled))
            return fail(error, "spherical clustering was cancelled");
        iterations = iteration;
        std::vector<std::uint64_t> counts(
            static_cast<std::size_t>(clusterCount), 0);
        for (std::size_t row = 0; row < sampleIndices.size(); ++row)
        {
            if ((row & 255u) == 0u && isCancelled(cancelled))
                return fail(error, "spherical clustering was cancelled");
            int bestCluster = 0;
            double bestCosine = dotDirection(directions, row, centroids[0]);
            for (int cluster = 1; cluster < clusterCount; ++cluster)
            {
                const double cosine = dotDirection(
                    directions, row,
                    centroids[static_cast<std::size_t>(cluster)]);
                if (cosine > bestCosine)
                {
                    bestCosine = cosine;
                    bestCluster = cluster;
                }
            }
            assignments[row] = bestCluster;
            ++counts[static_cast<std::size_t>(bestCluster)];
        }

        for (int empty = 0; empty < clusterCount; ++empty)
        {
            if (counts[static_cast<std::size_t>(empty)] != 0) continue;
            std::size_t selected = sampleIndices.size();
            double farthestDistance = -1.0;
            for (std::size_t row = 0; row < sampleIndices.size(); ++row)
            {
                if ((row & 255u) == 0u && isCancelled(cancelled))
                    return fail(error,
                                "spherical clustering was cancelled");
                const int donor = assignments[row];
                if (counts[static_cast<std::size_t>(donor)] <= 1) continue;
                const double distance = 1.0 - dotDirection(
                    directions, row,
                    centroids[static_cast<std::size_t>(donor)]);
                if (distance > farthestDistance)
                {
                    farthestDistance = distance;
                    selected = row;
                }
            }
            if (selected == sampleIndices.size())
                return fail(error,
                            "spherical clustering could not recover an empty cluster");
            const int donor = assignments[selected];
            --counts[static_cast<std::size_t>(donor)];
            assignments[selected] = empty;
            ++counts[static_cast<std::size_t>(empty)];
        }

        std::vector<std::vector<double>> updated(
            static_cast<std::size_t>(clusterCount),
            std::vector<double>(COMPONENT_COUNT, 0.0));
        for (std::size_t row = 0; row < sampleIndices.size(); ++row)
        {
            if ((row & 255u) == 0u && isCancelled(cancelled))
                return fail(error, "spherical clustering was cancelled");
            const std::size_t offset = row * COMPONENT_COUNT;
            std::vector<double>& sum =
                updated[static_cast<std::size_t>(assignments[row])];
            for (int component = 0; component < COMPONENT_COUNT; ++component)
                sum[static_cast<std::size_t>(component)] +=
                    directions[offset + static_cast<std::size_t>(component)];
        }

        double maximumShift = 0.0;
        for (int cluster = 0; cluster < clusterCount; ++cluster)
        {
            if (isCancelled(cancelled))
                return fail(error, "spherical clustering was cancelled");
            std::vector<double>& centroid =
                updated[static_cast<std::size_t>(cluster)];
            double squaredNorm = 0.0;
            for (double value : centroid) squaredNorm += value * value;
            if (squaredNorm <= 0.0 || !std::isfinite(squaredNorm))
            {
                const auto first = std::find(
                    assignments.begin(), assignments.end(), cluster);
                if (first == assignments.end())
                    return fail(error,
                                "spherical clustering centroid update failed");
                const std::size_t row = static_cast<std::size_t>(
                    std::distance(assignments.begin(), first));
                const std::size_t offset = row * COMPONENT_COUNT;
                for (int component = 0;
                     component < COMPONENT_COUNT; ++component)
                    centroid[static_cast<std::size_t>(component)] =
                        directions[offset +
                                   static_cast<std::size_t>(component)];
            }
            else
            {
                const double inverseNorm = 1.0 / std::sqrt(squaredNorm);
                for (double& value : centroid) value *= inverseNorm;
            }
            double squaredShift = 0.0;
            for (int component = 0; component < COMPONENT_COUNT; ++component)
            {
                const double difference =
                    centroid[static_cast<std::size_t>(component)] -
                    centroids[static_cast<std::size_t>(cluster)]
                             [static_cast<std::size_t>(component)];
                squaredShift += difference * difference;
            }
            maximumShift = std::max(maximumShift, std::sqrt(squaredShift));
        }

        const bool stableAssignments =
            !previousAssignments.empty() &&
            assignments == previousAssignments;
        previousAssignments = assignments;
        centroids = std::move(updated);
        if (stableAssignments && maximumShift <= CLUSTER_TOLERANCE)
        {
            converged = true;
            break;
        }
    }

    std::vector<std::uint64_t> populations(
        static_cast<std::size_t>(clusterCount), 0);
    std::vector<std::vector<double>> sums(
        static_cast<std::size_t>(clusterCount),
        std::vector<double>(COMPONENT_COUNT, 0.0));
    for (std::size_t row = 0; row < sampleIndices.size(); ++row)
    {
        if ((row & 255u) == 0u && isCancelled(cancelled))
            return fail(error, "spherical clustering was cancelled");
        const std::size_t cluster =
            static_cast<std::size_t>(assignments[row]);
        ++populations[cluster];
        const std::size_t offset = row * COMPONENT_COUNT;
        for (int component = 0; component < COMPONENT_COUNT; ++component)
            sums[cluster][static_cast<std::size_t>(component)] +=
                directions[offset + static_cast<std::size_t>(component)];
    }
    std::vector<double> concentrations(
        static_cast<std::size_t>(clusterCount), 0.0);
    std::vector<float> unsortedCentroids(
        static_cast<std::size_t>(clusterCount) * COMPONENT_COUNT, 0.0f);
    for (int cluster = 0; cluster < clusterCount; ++cluster)
    {
        if (isCancelled(cancelled))
            return fail(error, "spherical clustering was cancelled");
        double squaredNorm = 0.0;
        for (double value : sums[static_cast<std::size_t>(cluster)])
            squaredNorm += value * value;
        concentrations[static_cast<std::size_t>(cluster)] =
            std::sqrt(squaredNorm) /
            static_cast<double>(populations[static_cast<std::size_t>(cluster)]);
        for (int component = 0; component < COMPONENT_COUNT; ++component)
        {
            unsortedCentroids[
                static_cast<std::size_t>(cluster) * COMPONENT_COUNT +
                static_cast<std::size_t>(component)] =
                static_cast<float>(
                    centroids[static_cast<std::size_t>(cluster)]
                             [static_cast<std::size_t>(component)]);
        }
    }

    std::vector<int> order(static_cast<std::size_t>(clusterCount));
    std::iota(order.begin(), order.end(), 0);
    if (isCancelled(cancelled))
        return fail(error, "spherical clustering was cancelled");
    std::sort(order.begin(), order.end(),
              [&unsortedCentroids](int left, int right) {
                  return centroidLess(unsortedCentroids, left, right);
              });
    if (isCancelled(cancelled))
        return fail(error, "spherical clustering was cancelled");
    std::vector<int> remap(static_cast<std::size_t>(clusterCount), -1);
    std::vector<float> sortedCentroids;
    std::vector<std::uint64_t> sortedPopulations;
    std::vector<double> sortedConcentrations;
    sortedCentroids.reserve(
        static_cast<std::size_t>(clusterCount) * COMPONENT_COUNT);
    sortedPopulations.reserve(static_cast<std::size_t>(clusterCount));
    sortedConcentrations.reserve(static_cast<std::size_t>(clusterCount));
    for (int sorted = 0; sorted < clusterCount; ++sorted)
    {
        if (isCancelled(cancelled))
            return fail(error, "spherical clustering was cancelled");
        const int old = order[static_cast<std::size_t>(sorted)];
        remap[static_cast<std::size_t>(old)] = sorted;
        const std::size_t offset =
            static_cast<std::size_t>(old) * COMPONENT_COUNT;
        sortedCentroids.insert(
            sortedCentroids.end(),
            unsortedCentroids.begin() + static_cast<std::ptrdiff_t>(offset),
            unsortedCentroids.begin() + static_cast<std::ptrdiff_t>(
                offset + COMPONENT_COUNT));
        sortedPopulations.push_back(
            populations[static_cast<std::size_t>(old)]);
        sortedConcentrations.push_back(
            concentrations[static_cast<std::size_t>(old)]);
    }
    std::vector<int> fullAssignments(sampleCount, -1);
    for (std::size_t row = 0; row < sampleIndices.size(); ++row)
    {
        if ((row & 255u) == 0u && isCancelled(cancelled))
            return fail(error, "spherical clustering was cancelled");
        fullAssignments[sampleIndices[row]] =
            remap[static_cast<std::size_t>(assignments[row])];
    }

    ScienceAnalysisPayload candidate;
    candidate.kind = ScienceAnalysisKind::SphericalClusters;
    candidate.clusters.metric = ScienceMetric::CosineDistance;
    candidate.clusters.clusterCount = clusterCount;
    candidate.clusters.assignments =
        std::make_shared<const std::vector<int>>(
            std::move(fullAssignments));
    candidate.clusters.centroids =
        std::make_shared<const std::vector<float>>(
            std::move(sortedCentroids));
    candidate.clusters.populations =
        std::make_shared<const std::vector<std::uint64_t>>(
            std::move(sortedPopulations));
    candidate.clusters.concentrations =
        std::make_shared<const std::vector<double>>(
            std::move(sortedConcentrations));
    candidate.clusters.converged = converged;
    candidate.clusters.iterations = iterations;
    candidate.interpretation =
        std::make_shared<const std::vector<std::string>>(
            1, "Spherical clusters summarize local embedding directions by cosine similarity.");
    std::vector<std::string> limitations = {
        "Cluster ids are deterministic structural groups, not validated "
        "land-cover classes or physical class labels."};
    if (!converged)
        limitations.push_back(
            "Spherical k-means reached the 100-iteration limit without convergence.");
    candidate.limitations =
        std::make_shared<const std::vector<std::string>>(
            std::move(limitations));
    if (isCancelled(cancelled))
        return fail(error, "spherical clustering was cancelled");
    output = std::move(candidate);
    return true;
}
}
