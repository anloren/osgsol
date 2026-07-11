#include "ScienceEarthBuildContract.h"

#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

int main()
{
    CHECK(OSGSOL_BUILD_SCIENCE_VALUE == 0);
    CHECK(std::string(OSGSOL_SCIENCE_PHASE_VALUE) == "G0-G1");

    const std::string targets = OSGSOL_BUILD_TARGETS_VALUE;
    CHECK(targets.find("osgSolScienceCore") == std::string::npos);
    CHECK(targets.find("osgdb_science") == std::string::npos);

    std::cout << "[OK] ScienceEarth G0-G1 science-off build contract\n";
    return 0;
}
