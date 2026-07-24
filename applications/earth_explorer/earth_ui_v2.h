#ifndef EARTH_UI_V2_H
#define EARTH_UI_V2_H

#include "earth_control_layout.h"

#include <string>

namespace earthui
{

struct EarthUiShellState
{
    EarthUiModule activeModule = EarthUiModule::Explore;
    bool drawerOpen = true;
    bool aboutOpen = false;
};

struct EarthUiTopBarData
{
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double altitudeKm = 0.0;
    bool aiBusy = false;
    bool scienceAvailable = false;
};

const char* moduleLabel(EarthUiModule module);
const char* moduleShortLabel(EarthUiModule module);
const char* moduleContextLabel(EarthUiModule module);
bool moduleAcceptsLayerGroup(EarthUiModule module, const std::string& group);
void activateEarthUiModule(EarthUiShellState& state, EarthUiModule module);

// Carbon Spectrum is an application-level theme. It intentionally leaves the
// renderer/backend untouched and changes only the Earth application's visual
// language and component metrics.
void applyEarthUiV2Theme();

// Returns true when the user requests the global/home camera action.
bool drawEarthUiTopBar(const EarthUiShellLayout& layout,
                       EarthUiShellState& state,
                       const EarthUiTopBarData& data);

void drawEarthUiModuleRail(const EarthUiShellLayout& layout,
                           EarthUiShellState& state);

bool beginEarthUiModuleDrawer(const EarthUiShellLayout& layout,
                              const EarthUiShellState& state);
void endEarthUiModuleDrawer();

void drawEarthUiContextTray(const EarthUiShellLayout& layout,
                            const EarthUiShellState& state);

}

#endif
