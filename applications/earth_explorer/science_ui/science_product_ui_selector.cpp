#include "science_product_ui_selector.h"

std::string normalizeScienceProductUiSelector(
    const std::string& environmentValue,
    const std::string& compiledDefault)
{
    const std::string selected = environmentValue.empty()
        ? compiledDefault : environmentValue;
    return selected == "rml" || selected == "legacy"
        ? selected : "legacy";
}

ScienceProductUiDecision decideScienceProductUi(
    const std::string& selector, bool rmlBuilt, bool scienceAvailable,
    bool workbenchAbiAvailable, bool rmlRuntimeReady,
    bool rmlRuntimeFailed)
{
    ScienceProductUiDecision output;
    const bool eligible = selector == "rml" && rmlBuilt &&
        scienceAvailable && workbenchAbiAvailable;
    output.attemptRmlInitialization = eligible && !rmlRuntimeFailed;
    output.loadScienceDocuments = output.attemptRmlInitialization;
    output.renderRmlScience = eligible && rmlRuntimeReady &&
        !rmlRuntimeFailed;
    output.renderLegacyScience = scienceAvailable && !output.renderRmlScience;
    return output;
}
