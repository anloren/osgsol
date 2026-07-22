#include "science_ui/science_product_ui_selector.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x "\n"; return 1; } } while (0)

int main()
{
    const ScienceProductUiDecision ready = decideScienceProductUi(
        "rml", true, true, true, true, false);
    CHECK(ready.attemptRmlInitialization);
    CHECK(ready.renderRmlScience);
    CHECK(!ready.renderLegacyScience);

    const ScienceProductUiDecision pending = decideScienceProductUi(
        "rml", true, true, true, false, false);
    CHECK(pending.attemptRmlInitialization);
    CHECK(!pending.renderRmlScience);
    CHECK(pending.renderLegacyScience);

    const ScienceProductUiDecision failed = decideScienceProductUi(
        "rml", true, true, true, false, true);
    CHECK(!failed.renderRmlScience);
    CHECK(failed.renderLegacyScience);

    const ScienceProductUiDecision oldAbi = decideScienceProductUi(
        "rml", true, true, false, true, false);
    CHECK(!oldAbi.attemptRmlInitialization);
    CHECK(oldAbi.renderLegacyScience);

    const ScienceProductUiDecision forcedLegacy = decideScienceProductUi(
        "legacy", true, true, true, true, false);
    CHECK(!forcedLegacy.attemptRmlInitialization);
    CHECK(forcedLegacy.renderLegacyScience);

    const ScienceProductUiDecision scienceOff = decideScienceProductUi(
        "rml", true, false, false, true, false);
    CHECK(!scienceOff.attemptRmlInitialization);
    CHECK(!scienceOff.loadScienceDocuments);

    CHECK(normalizeScienceProductUiSelector("", "rml") == "rml");
    CHECK(normalizeScienceProductUiSelector("legacy", "rml") == "legacy");
    CHECK(normalizeScienceProductUiSelector("invalid", "rml") == "legacy");

    std::cout << "[OK] ScienceEarth product UI offline selector\n";
    return 0;
}
