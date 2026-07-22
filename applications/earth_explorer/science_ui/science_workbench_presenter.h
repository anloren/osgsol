#ifndef EARTH_SCIENCE_UI_WORKBENCH_PRESENTER_H
#define EARTH_SCIENCE_UI_WORKBENCH_PRESENTER_H

#include "../product_ui/rml_ui_runtime.h"
#include "science_target_overlay.h"

#include <RmlUi/Core/EventListener.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

class SciencePluginRuntime;
class MapContextCapture;

struct ScienceWorkbenchQueuedAction
{
    std::string name;
    std::string json;
};

// RmlUi presenter for the scientific query composer. It never invokes the
// plugin or camera from the render callback. UI events are copied into a
// bounded queue and drained by the viewer's main event traversal.
class ScienceWorkbenchPresenter : public RmlUiFrameClient,
                                  public Rml::EventListener
{
public:
    ScienceWorkbenchPresenter(SciencePluginRuntime& runtime,
                              std::string documentPath,
                              MapContextCapture* contextCapture = nullptr);
    ~ScienceWorkbenchPresenter() override;

    bool onRmlContextReady(Rml::Context& context,
                           std::string& error) override;
    void onRmlFrame(Rml::Context& context) override;
    void ProcessEvent(Rml::Event& event) override;

    bool takeQueuedAction(ScienceWorkbenchQueuedAction& action);
    bool latestTarget(ScienceTargetOverlayInput& target) const;
    void publishActionError(const std::string& error);
    void setVisible(bool visible);

private:
    class Impl;
    Impl* _impl;
};

#endif
