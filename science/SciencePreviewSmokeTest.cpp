#include "SciencePreviewRuntime.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::cerr << "usage: science-preview-smoke INDEX LAT LON YEAR\n";
        return 2;
    }

    earthscience::SciencePreviewRuntime runtime(argv[1]);
    if (!runtime.available())
    {
        std::cerr << "AlphaEarth index is unavailable\n";
        return 1;
    }

    runtime.queryPoint(std::strtod(argv[2], nullptr),
                       std::strtod(argv[3], nullptr),
                       std::atoi(argv[4]));
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const earthscience::SciencePreviewSnapshot snapshot =
            runtime.snapshot();
        if (snapshot.state == earthscience::PreviewState::Ready)
        {
            const std::size_t expected =
                static_cast<std::size_t>(snapshot.artifact.width) *
                static_cast<std::size_t>(snapshot.artifact.height) * 4;
            if (!snapshot.artifact.rgba ||
                snapshot.artifact.rgba->size() != expected ||
                snapshot.artifact.width != 256 ||
                snapshot.artifact.height != 256)
            {
                std::cerr << "AlphaEarth preview has invalid pixels\n";
                return 1;
            }
            std::cout << "READY " << snapshot.artifact.datasetId << " "
                      << snapshot.artifact.width << "x"
                      << snapshot.artifact.height << "\n";
            return 0;
        }
        if (snapshot.state == earthscience::PreviewState::Failed ||
            snapshot.state == earthscience::PreviewState::Unavailable)
        {
            std::cerr << snapshot.message << "\n";
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cerr << "AlphaEarth preview timed out\n";
    return 1;
}
