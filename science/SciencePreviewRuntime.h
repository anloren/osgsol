#ifndef OSGSOL_SCIENCE_PREVIEW_RUNTIME_H
#define OSGSOL_SCIENCE_PREVIEW_RUNTIME_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "SciencePreviewSupport.h"

namespace earthscience
{
    enum class AlphaEarthPreviewState
    {
        Unavailable,
        Idle,
        Queued,
        Fetching,
        Ready,
        Failed,
        Cancelled,
    };

    struct AlphaEarthSourceDescriptor
    {
        std::string id = "alphaearth-foundations";
        std::string name = "AlphaEarth Foundations";
        std::string version = "1.1";
        std::string attribution =
            "Google / Google DeepMind · source.coop · CC-BY 4.0";
        int firstYear = 2017;
        int lastYear = 2025;
        int nativeResolutionMeters = 10;
        int bandCount = 64;
        bool experimental = true;
        std::string visualization = "false-color embedding composite";
        std::string redBand = "A01";
        std::string greenBand = "A16";
        std::string blueBand = "A09";
        double displayMinimum = -0.3;
        double displayMaximum = 0.3;
    };

    struct AlphaEarthPreviewArtifact
    {
        std::uint64_t generation = 0;
        std::string datasetId;
        std::string sourceUrl;
        std::string sourceVersion;
        std::string attribution;
        int year = 0;
        double west = 0.0;
        double south = 0.0;
        double east = 0.0;
        double north = 0.0;
        int width = 0;
        int height = 0;
        int sourceWindowWidth = 0;
        int sourceWindowHeight = 0;
        double sourceResolutionMeters = 0.0;
        double displayResolutionMeters = 0.0;
        std::shared_ptr<const std::vector<unsigned char>> rgba;
        std::shared_ptr<const ScienceGroundGrid> groundGrid;
    };

    struct AlphaEarthPreviewSnapshot
    {
        AlphaEarthPreviewState state = AlphaEarthPreviewState::Unavailable;
        std::uint64_t generation = 0;
        double latitude = 0.0;
        double longitude = 0.0;
        int year = 2025;
        float progress = 0.0f;
        double elapsedSeconds = 0.0;
        std::string message;
        AlphaEarthPreviewArtifact artifact;
    };

    class SciencePreviewRuntime
    {
    public:
        explicit SciencePreviewRuntime(const std::string& indexPath);
        ~SciencePreviewRuntime();

        SciencePreviewRuntime(const SciencePreviewRuntime&) = delete;
        SciencePreviewRuntime& operator=(const SciencePreviewRuntime&) = delete;

        bool available() const;
        const AlphaEarthSourceDescriptor& source() const;
        std::uint64_t queryPoint(double latitude, double longitude, int year,
                                 double requestedSpanMeters = 0.0);
        void cancel();
        void clear();
        AlphaEarthPreviewSnapshot snapshot() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}

#endif
