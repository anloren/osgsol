#ifndef EARTH_SCIENCE_UI_PRODUCT_UI_SELECTOR_H
#define EARTH_SCIENCE_UI_PRODUCT_UI_SELECTOR_H

#include <string>

struct ScienceProductUiDecision
{
    bool attemptRmlInitialization = false;
    bool loadScienceDocuments = false;
    bool renderRmlScience = false;
    bool renderLegacyScience = false;
};

std::string normalizeScienceProductUiSelector(
    const std::string& environmentValue,
    const std::string& compiledDefault);

ScienceProductUiDecision decideScienceProductUi(
    const std::string& selector,
    bool rmlBuilt,
    bool scienceAvailable,
    bool workbenchAbiAvailable,
    bool rmlRuntimeReady,
    bool rmlRuntimeFailed);

#endif
