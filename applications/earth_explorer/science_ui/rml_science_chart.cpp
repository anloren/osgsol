#include "rml_science_chart.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float PAD_LEFT = 18.0f;
constexpr float PAD_RIGHT = 18.0f;
constexpr float PAD_TOP = 18.0f;
constexpr float PAD_BOTTOM = 24.0f;

void addSegment(Rml::Mesh& mesh, const Rml::Vector2f& from,
                const Rml::Vector2f& to, float width,
                Rml::ColourbPremultiplied colour)
{
    const Rml::Vector2f direction = to - from;
    const float length = std::sqrt(direction.x * direction.x +
                                   direction.y * direction.y);
    if (!(length > 0.001f)) return;
    const Rml::Vector2f normal(-direction.y * width / (2.0f * length),
                               direction.x * width / (2.0f * length));
    const int vertex = static_cast<int>(mesh.vertices.size());
    mesh.vertices.resize(mesh.vertices.size() + 4);
    mesh.indices.insert(mesh.indices.end(), {
        vertex, vertex + 1, vertex + 2,
        vertex, vertex + 2, vertex + 3});
    mesh.vertices[vertex + 0] = {from + normal, colour, {0.0f, 0.0f}};
    mesh.vertices[vertex + 1] = {from - normal, colour, {0.0f, 0.0f}};
    mesh.vertices[vertex + 2] = {to - normal, colour, {0.0f, 0.0f}};
    mesh.vertices[vertex + 3] = {to + normal, colour, {0.0f, 0.0f}};
}

Rml::Vector2f pointPosition(const ScienceChartModel& model,
                            std::size_t index, float width, float height)
{
    const float plotWidth = std::max(1.0f, width - PAD_LEFT - PAD_RIGHT);
    const float plotHeight = std::max(1.0f, height - PAD_TOP - PAD_BOTTOM);
    const float fraction = model.points.size() > 1
        ? static_cast<float>(index) /
            static_cast<float>(model.points.size() - 1)
        : 0.5f;
    const double range = model.domainMaximum - model.domainMinimum;
    const float y = range > 0.0
        ? static_cast<float>((model.domainMaximum - model.points[index].value) /
                             range)
        : 0.5f;
    return {PAD_LEFT + fraction * plotWidth,
            PAD_TOP + std::clamp(y, 0.0f, 1.0f) * plotHeight};
}
}

RmlScienceChart::RmlScienceChart(const Rml::String& tag)
    : Rml::Element(tag)
{
}

void RmlScienceChart::setModel(const ScienceChartModel& model,
                               int selectedYear)
{
    _model = model;
    _selectedYear = selectedYear;
    _hoveredPoint = -1;
}

bool RmlScienceChart::consumeSelectedYear(int& year)
{
    if (!_hasPendingSelection) return false;
    year = _pendingSelectedYear;
    _hasPendingSelection = false;
    return true;
}

int RmlScienceChart::nearestPoint(float absoluteMouseX)
{
    if (_model.points.empty()) return -1;
    const float width = GetClientWidth();
    const float plotWidth = std::max(1.0f, width - PAD_LEFT - PAD_RIGHT);
    const float local = std::clamp(
        absoluteMouseX - GetAbsoluteOffset().x - PAD_LEFT, 0.0f, plotWidth);
    const float scaled = _model.points.size() > 1
        ? local / plotWidth * static_cast<float>(_model.points.size() - 1)
        : 0.0f;
    return std::clamp(static_cast<int>(std::lround(scaled)), 0,
                      static_cast<int>(_model.points.size() - 1));
}

void RmlScienceChart::ProcessEvent(Rml::Event& event)
{
    if (event == Rml::EventId::Mousemove)
    {
        _hoveredPoint = nearestPoint(
            event.GetParameter<float>("mouse_x", 0.0f));
    }
    else if (event == Rml::EventId::Mouseout)
        _hoveredPoint = -1;
    else if (event == Rml::EventId::Click)
    {
        const int index = nearestPoint(
            event.GetParameter<float>("mouse_x", 0.0f));
        if (index >= 0)
        {
            _selectedYear = _model.points[index].year;
            _pendingSelectedYear = _selectedYear;
            _hasPendingSelection = true;
        }
    }
}

void RmlScienceChart::OnChildAdd(Rml::Element* element)
{
    Rml::Element::OnChildAdd(element);
    if (element == this)
    {
        AddEventListener(Rml::EventId::Mousemove, this);
        AddEventListener(Rml::EventId::Mouseout, this);
        AddEventListener(Rml::EventId::Click, this);
    }
}

void RmlScienceChart::OnChildRemove(Rml::Element* element)
{
    if (element == this)
    {
        RemoveEventListener(Rml::EventId::Mousemove, this);
        RemoveEventListener(Rml::EventId::Mouseout, this);
        RemoveEventListener(Rml::EventId::Click, this);
    }
    Rml::Element::OnChildRemove(element);
}

void RmlScienceChart::OnRender()
{
    if (_model.points.empty() || !GetContext()) return;
    const float width = GetClientWidth();
    const float height = GetClientHeight();
    if (width < PAD_LEFT + PAD_RIGHT + 2.0f ||
        height < PAD_TOP + PAD_BOTTOM + 2.0f)
        return;

    Rml::Mesh mesh;
    const Rml::ColourbPremultiplied grid(54, 66, 73, 160);
    const Rml::ColourbPremultiplied series(40, 201, 195, 255);
    const Rml::ColourbPremultiplied point(167, 239, 233, 255);
    const Rml::ColourbPremultiplied selected(225, 189, 98, 255);
    const Rml::ColourbPremultiplied missing(147, 160, 158, 150);

    const float plotWidth = width - PAD_LEFT - PAD_RIGHT;
    const float plotHeight = height - PAD_TOP - PAD_BOTTOM;
    for (int row = 0; row <= 4; ++row)
    {
        const float y = PAD_TOP + plotHeight * static_cast<float>(row) / 4.0f;
        Rml::MeshUtilities::GenerateLine(
            mesh, {PAD_LEFT, y}, {plotWidth, 1.0f}, grid);
    }

    for (std::size_t index = 1; index < _model.points.size(); ++index)
    {
        if (_model.points[index - 1].missing || _model.points[index].missing)
            continue;
        addSegment(mesh, pointPosition(_model, index - 1, width, height),
                   pointPosition(_model, index, width, height), 2.25f, series);
    }
    // Mark missing years on the baseline and valid observations on the curve.
    for (std::size_t index = 0; index < _model.points.size(); ++index)
    {
        const float fraction = _model.points.size() > 1
            ? static_cast<float>(index) /
                static_cast<float>(_model.points.size() - 1)
            : 0.5f;
        const float x = PAD_LEFT + fraction * plotWidth;
        if (_model.points[index].missing)
        {
            Rml::MeshUtilities::GenerateQuad(
                mesh, {x - 2.0f, PAD_TOP + plotHeight - 2.0f},
                {4.0f, 4.0f}, missing);
            continue;
        }
        const Rml::Vector2f position = pointPosition(_model, index, width, height);
        const bool emphasized = _model.points[index].year == _selectedYear ||
            static_cast<int>(index) == _hoveredPoint;
        const float radius = emphasized ? 4.5f : 3.0f;
        Rml::MeshUtilities::GenerateQuad(
            mesh, position - Rml::Vector2f(radius, radius),
            {radius * 2.0f, radius * 2.0f},
            _model.points[index].year == _selectedYear ? selected : point);
    }

    if (_hoveredPoint >= 0 &&
        _hoveredPoint < static_cast<int>(_model.points.size()))
    {
        const float fraction = _model.points.size() > 1
            ? static_cast<float>(_hoveredPoint) /
                static_cast<float>(_model.points.size() - 1)
            : 0.5f;
        const float x = PAD_LEFT + fraction * plotWidth;
        Rml::MeshUtilities::GenerateLine(
            mesh, {x, PAD_TOP}, {1.0f, plotHeight}, selected);
    }

    if (mesh)
    {
        Rml::Geometry geometry = GetContext()->GetRenderManager().MakeGeometry(
            std::move(mesh));
        geometry.Render(GetAbsoluteOffset());
    }
}

void registerRmlScienceChartElement()
{
    static Rml::ElementInstancerGeneric<RmlScienceChart> instancer;
    static bool registered = false;
    if (!registered)
    {
        Rml::Factory::RegisterElementInstancer("science-chart", &instancer);
        registered = true;
    }
}
