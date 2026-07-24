#ifndef EARTH_UI_COMPONENTS_H
#define EARTH_UI_COMPONENTS_H

#include "earth_ui_tokens.h"

#include <algorithm>
#include <string>

namespace earthui
{

enum class StatusTone
{
    Neutral,
    Active,
    Measure,
    Success,
    Danger,
};

inline const ImVec4& statusToneColor(StatusTone tone)
{
    switch (tone)
    {
    case StatusTone::Active: return design::kCyan;
    case StatusTone::Measure: return design::kMeasure;
    case StatusTone::Success: return design::kSuccess;
    case StatusTone::Danger: return design::kDanger;
    case StatusTone::Neutral:
    default: return design::kTextDim;
    }
}

// One shared section component for every native module drawer. The visible
// title remains short; detail belongs in the optional summary shown only while
// the section is open. This prevents the old bilingual paragraph headings from
// competing with controls.
inline bool beginDrawerSection(const char* id, const char* title,
                               const char* summary = nullptr,
                               bool defaultOpen = true)
{
    std::string label = title ? title : "";
    label += "###drawer_section_";
    label += id ? id : "unnamed";

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_Framed |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_FramePadding;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 7.0f));
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    ImGui::PopStyleVar();
    if (open && summary && summary[0])
    {
        ImGui::PushStyleColor(ImGuiCol_Text, design::kTextDim);
        ImGui::TextWrapped("%s", summary);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    return open;
}

inline void endDrawerSection()
{
    ImGui::TreePop();
    ImGui::Spacing();
}

inline void drawStatusLine(StatusTone tone, const char* text)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float lineHeight = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(
        origin, ImVec2(origin.x + 3.0f, origin.y + lineHeight),
        ImGui::ColorConvertFloat4ToU32(statusToneColor(tone)), 1.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 9.0f);
    ImGui::PushStyleColor(ImGuiCol_Text,
        tone == StatusTone::Neutral ? design::kText : statusToneColor(tone));
    ImGui::TextWrapped("%s", text ? text : "");
    ImGui::PopStyleColor();
}

inline bool fullWidthButton(const char* label, StatusTone tone,
                            bool enabled = true)
{
    if (!enabled) ImGui::BeginDisabled();
    if (tone == StatusTone::Active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, design::kCyan);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, design::kTextStrong);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, design::kMeasure);
        ImGui::PushStyleColor(ImGuiCol_Text, design::kCarbon);
    }
    else if (tone == StatusTone::Danger)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, design::kOxblood);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, design::kVermilion);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, design::kDanger);
        ImGui::PushStyleColor(ImGuiCol_Text, design::kTextStrong);
    }
    const bool pressed = ImGui::Button(label, ImVec2(-1.0f, 0.0f));
    if (tone == StatusTone::Active || tone == StatusTone::Danger)
        ImGui::PopStyleColor(4);
    if (!enabled) ImGui::EndDisabled();
    return pressed;
}

// Continue a compact action row only when the next control fits. Otherwise the
// control naturally starts on the next line instead of being clipped.
inline void continueRowIfFits(float nextControlWidth)
{
    const float right = ImGui::GetWindowPos().x +
        ImGui::GetWindowContentRegionMax().x;
    const float nextRight = ImGui::GetItemRectMax().x +
        ImGui::GetStyle().ItemSpacing.x + std::max(0.0f, nextControlWidth);
    if (nextRight <= right) ImGui::SameLine();
}

inline float buttonWidth(const char* label)
{
    return ImGui::CalcTextSize(label ? label : "").x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
}

}

#endif
