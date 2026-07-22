#ifndef EARTH_SCIENCE_UI_RML_SCIENCE_CHART_H
#define EARTH_SCIENCE_UI_RML_SCIENCE_CHART_H

#include "science_chart_model.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>

class RmlScienceChart : public Rml::Element, public Rml::EventListener
{
public:
    explicit RmlScienceChart(const Rml::String& tag);

    void setModel(const ScienceChartModel& model, int selectedYear);
    bool consumeSelectedYear(int& year);

    void ProcessEvent(Rml::Event& event) override;
    void OnChildAdd(Rml::Element* element) override;
    void OnChildRemove(Rml::Element* element) override;

protected:
    void OnRender() override;

private:
    int nearestPoint(float absoluteMouseX);

    ScienceChartModel _model;
    int _selectedYear = 0;
    int _hoveredPoint = -1;
    int _pendingSelectedYear = 0;
    bool _hasPendingSelection = false;
};

// Register once after RmlUi has initialized and before report.rml is loaded.
void registerRmlScienceChartElement();

#endif
