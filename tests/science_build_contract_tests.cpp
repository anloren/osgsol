#include "ScienceEarthBuildContract.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

int main()
{
    CHECK(std::string(OSGSOL_SCIENCE_PHASE_VALUE) == "G2-1");

    const std::string targets = OSGSOL_BUILD_TARGETS_VALUE;
    if (OSGSOL_BUILD_SCIENCE_VALUE == 1)
        CHECK(targets.find("osgSolScienceCore") != std::string::npos);
    else
    {
        CHECK(OSGSOL_BUILD_SCIENCE_VALUE == 0);
        CHECK(targets.find("osgSolScienceCore") == std::string::npos);
        CHECK(targets.find("osgdb_science") == std::string::npos);
    }

    std::ifstream rootCMakeFile(std::string(OSGSOL_SOURCE_DIR) + "/CMakeLists.txt");
    CHECK(rootCMakeFile.good());
    std::ostringstream rootCMakeBuffer;
    rootCMakeBuffer << rootCMakeFile.rdbuf();
    CHECK(rootCMakeBuffer.str().find("$<TARGET_EXISTS:") == std::string::npos);

    std::cout << "[OK] ScienceEarth G2-1 build contract\n";
    return 0;
}
