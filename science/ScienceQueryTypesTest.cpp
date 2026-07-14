#include "ScienceQueryTypes.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    void testStableStateNames()
    {
        require(std::string(earthscience::scienceSourceHealthName(
                    earthscience::ScienceSourceHealth::Unavailable)) ==
                    "unavailable",
                "unavailable source health name changed");
        require(std::string(earthscience::scienceSourceHealthName(
                    earthscience::ScienceSourceHealth::Degraded)) ==
                    "degraded",
                "degraded source health name changed");
        require(std::string(earthscience::scienceJobStateName(
                    earthscience::ScienceJobState::Fetching)) == "fetching",
                "fetching job state name changed");
        require(std::string(earthscience::scienceJobStateName(
                    static_cast<earthscience::ScienceJobState>(999))) ==
                    "unknown",
                "unknown job states must remain machine-readable");
    }

    void testGenericDefaultsAreUnavailableAndBounded()
    {
        earthscience::ScienceSourceDescriptor source;
        earthscience::GeoTemporalQuery query;
        earthscience::ScienceJobSnapshot snapshot;

        require(source.health == earthscience::ScienceSourceHealth::Unavailable,
                "a default source must not claim availability");
        require(query.geometry.kind == earthscience::ScienceGeometryKind::Point,
                "the bounded G2 query default must be a point");
        require(query.time.mode == earthscience::ScienceTimeMode::ExplicitYears,
                "the bounded G2 query default must use explicit years");
        require(query.aggregation == earthscience::ScienceAggregation::None,
                "the bounded G2 query default must not aggregate");
        require(query.outputKind == earthscience::ScienceOutputKind::RasterLayer,
                "the bounded G2 query default must request a raster layer");
        require(snapshot.state == earthscience::ScienceJobState::Idle,
                "a default service snapshot must be idle");
        require(!snapshot.lastSuccessfulArtifact,
                "a default service snapshot must not invent an artifact");
    }

    void testArtifactSharesImmutablePixelsAndExactGroundGrid()
    {
        static_assert(std::is_same<
            decltype(earthscience::ScienceRasterPayload::rgba),
            std::shared_ptr<const std::vector<unsigned char>>>::value,
            "science RGBA payload must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceRasterPayload::groundGrid),
            std::shared_ptr<const earthscience::ScienceGroundGrid>>::value,
            "science ground grid must be immutable");

        earthscience::ScienceArtifact artifact;
        artifact.artifactId = "artifact-7";
        artifact.generation = 7;
        artifact.raster.width = 2;
        artifact.raster.height = 2;
        artifact.raster.rgba =
            std::make_shared<const std::vector<unsigned char>>(16, 166);
        auto grid = std::make_shared<earthscience::ScienceGroundGrid>();
        grid->columns = 2;
        grid->rows = 2;
        grid->points = {
            {138.70, 35.34}, {138.76, 35.34},
            {138.70, 35.40}, {138.76, 35.40},
        };
        artifact.raster.groundGrid = grid;

        const earthscience::ScienceArtifact copied = artifact;
        require(copied.artifactId == "artifact-7" && copied.generation == 7,
                "artifact value metadata was not copied");
        require(copied.raster.rgba == artifact.raster.rgba,
                "artifact copy duplicated or lost immutable pixels");
        require(copied.raster.groundGrid == artifact.raster.groundGrid,
                "artifact copy duplicated or lost the exact ground grid");
    }
}

int main()
{
    testStableStateNames();
    testGenericDefaultsAreUnavailableAndBounded();
    testArtifactSharesImmutablePixelsAndExactGroundGrid();
    std::cout << "[OK] ScienceEarth generic query type contract\n";
    return 0;
}
