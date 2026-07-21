#ifndef EARTH_UI_CHART_H
#define EARTH_UI_CHART_H

#include <ui/ImGuiComponents.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace earthui
{

inline bool drawAnnualSeriesChart(
    const char* id, const char* label,
    const std::vector<int>* years,
    const std::vector<double>* values,
    const std::vector<unsigned char>* validity,
    const std::string& unit)
{
    if (!values || values->empty()) return false;

    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    std::size_t validCount = 0;
    for (std::size_t index = 0; index < values->size(); ++index)
    {
        const bool valid = !validity || index >= validity->size() ||
            (*validity)[index] != 0;
        const double value = (*values)[index];
        if (!valid || !std::isfinite(value)) continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        ++validCount;
    }
    if (validCount == 0) return false;
    if (minimum == maximum)
    {
        const double pad = std::max(0.01, std::abs(minimum) * 0.05);
        minimum -= pad;
        maximum += pad;
    }
    else
    {
        const double pad = (maximum - minimum) * 0.08;
        minimum -= pad;
        maximum += pad;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.22f, 0.77f, 0.88f, 1.0f));
    ImGui::TextUnformatted(label ? label : "");
    ImGui::PopStyleColor();
    if (!unit.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", unit.c_str());
    }

    const float width = std::max(180.0f, ImGui::GetContentRegionAvail().x);
    const float height = 172.0f;
    ImGui::InvisibleButton(id, ImVec2(width, height));
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 background = IM_COL32(12, 16, 18, 246);
    const ImU32 border = IM_COL32(82, 62, 58, 220);
    const ImU32 grid = IM_COL32(90, 102, 107, 60);
    const ImU32 axisText = IM_COL32(126, 137, 142, 230);
    const ImU32 seriesColor = IM_COL32(56, 195, 223, 255);
    const ImU32 hoverColor = IM_COL32(224, 70, 45, 255);
    draw->AddRectFilled(itemMin, itemMax, background, 2.0f);
    draw->AddRect(itemMin, itemMax, border, 2.0f);

    const ImVec2 plotMin(itemMin.x + 48.0f, itemMin.y + 10.0f);
    const ImVec2 plotMax(itemMax.x - 10.0f, itemMax.y - 28.0f);
    const float plotWidth = std::max(1.0f, plotMax.x - plotMin.x);
    const float plotHeight = std::max(1.0f, plotMax.y - plotMin.y);
    char buffer[64];
    for (int gridIndex = 0; gridIndex <= 4; ++gridIndex)
    {
        const float t = static_cast<float>(gridIndex) / 4.0f;
        const float y = plotMax.y - t * plotHeight;
        draw->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), grid);
        const double value = minimum + t * (maximum - minimum);
        std::snprintf(buffer, sizeof(buffer), "%.3g", value);
        draw->AddText(ImVec2(itemMin.x + 5.0f, y - 7.0f), axisText, buffer);
    }

    const auto xAt = [&](std::size_t index) {
        if (values->size() <= 1) return plotMin.x + plotWidth * 0.5f;
        return plotMin.x + plotWidth *
            static_cast<float>(index) /
            static_cast<float>(values->size() - 1);
    };
    const auto yAt = [&](double value) {
        const double normalized = (value - minimum) / (maximum - minimum);
        return plotMax.y - plotHeight * static_cast<float>(normalized);
    };

    bool previousValid = false;
    ImVec2 previous;
    for (std::size_t index = 0; index < values->size(); ++index)
    {
        const bool valid = (!validity || index >= validity->size() ||
            (*validity)[index] != 0) && std::isfinite((*values)[index]);
        if (!valid)
        {
            previousValid = false;
            continue;
        }
        const ImVec2 point(xAt(index), yAt((*values)[index]));
        if (previousValid) draw->AddLine(previous, point, seriesColor, 2.0f);
        draw->AddCircleFilled(point, 2.6f, seriesColor);
        previous = point;
        previousValid = true;
    }

    if (years && !years->empty())
    {
        const std::size_t middle = years->size() / 2;
        std::snprintf(buffer, sizeof(buffer), "%d", years->front());
        draw->AddText(ImVec2(plotMin.x, plotMax.y + 7.0f), axisText, buffer);
        if (years->size() > 2)
        {
            std::snprintf(buffer, sizeof(buffer), "%d", (*years)[middle]);
            const ImVec2 size = ImGui::CalcTextSize(buffer);
            draw->AddText(ImVec2(xAt(middle) - size.x * 0.5f,
                                 plotMax.y + 7.0f), axisText, buffer);
        }
        std::snprintf(buffer, sizeof(buffer), "%d", years->back());
        const ImVec2 size = ImGui::CalcTextSize(buffer);
        draw->AddText(ImVec2(plotMax.x - size.x, plotMax.y + 7.0f),
                      axisText, buffer);
    }

    if (ImGui::IsItemHovered() && values->size() > 0)
    {
        const float normalized = std::max(0.0f, std::min(1.0f,
            (ImGui::GetIO().MousePos.x - plotMin.x) / plotWidth));
        std::size_t index = static_cast<std::size_t>(std::round(
            normalized * static_cast<float>(values->size() - 1)));
        if (index >= values->size()) index = values->size() - 1;
        const bool valid = (!validity || index >= validity->size() ||
            (*validity)[index] != 0) && std::isfinite((*values)[index]);
        if (valid)
        {
            const ImVec2 point(xAt(index), yAt((*values)[index]));
            draw->AddLine(ImVec2(point.x, plotMin.y),
                          ImVec2(point.x, plotMax.y), hoverColor, 1.0f);
            draw->AddCircleFilled(point, 4.2f, hoverColor);
            if (years && index < years->size())
                ImGui::SetTooltip("%d\n%.6g %s", (*years)[index],
                                  (*values)[index], unit.c_str());
            else
                ImGui::SetTooltip("%.6g %s", (*values)[index], unit.c_str());
        }
        else ImGui::SetTooltip(u8"该年份无有效数据");
    }
    return true;
}

}

#endif
