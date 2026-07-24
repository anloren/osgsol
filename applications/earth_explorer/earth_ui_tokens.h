#ifndef EARTH_UI_TOKENS_H
#define EARTH_UI_TOKENS_H

#include "3rdparty/imgui/imgui.h"

namespace earthui
{
namespace design
{

// Obsidian Survey is the single product palette for both ImGui and RmlUi.
// Keep the hexadecimal comments in sync with
// assets/misc/ui/scienceearth/tokens.rcss.
static const ImVec4 kCarbon(0.027f, 0.035f, 0.039f, 0.980f);       // #07090a
static const ImVec4 kIron(0.051f, 0.063f, 0.067f, 0.985f);         // #0d1011
static const ImVec4 kRaisedIron(0.078f, 0.090f, 0.098f, 0.990f);   // #141719
static const ImVec4 kHoverIron(0.102f, 0.118f, 0.125f, 0.995f);    // #1a1e20
static const ImVec4 kOxblood(0.220f, 0.059f, 0.047f, 0.980f);      // #380f0c
static const ImVec4 kVermilion(0.827f, 0.192f, 0.137f, 1.000f);   // #d33123
static const ImVec4 kCyan(0.220f, 0.765f, 0.875f, 1.000f);        // #38c3df
static const ImVec4 kMeasure(0.882f, 0.741f, 0.384f, 1.000f);     // #e1bd62
static const ImVec4 kDanger(0.937f, 0.416f, 0.384f, 1.000f);      // #ef6a62
static const ImVec4 kSuccess(0.361f, 0.722f, 0.416f, 1.000f);     // #5cb86a
static const ImVec4 kText(0.655f, 0.667f, 0.651f, 1.000f);        // #a7aaa6
static const ImVec4 kTextStrong(0.898f, 0.894f, 0.875f, 1.000f);  // #e5e4df
static const ImVec4 kTextDim(0.451f, 0.478f, 0.471f, 1.000f);     // #737a78
static const ImVec4 kBorder(0.231f, 0.188f, 0.180f, 0.920f);      // #3b302e
static const ImVec4 kBorderStrong(0.333f, 0.255f, 0.239f, 1.0f);  // #55413d

// Shared conversion helpers keep custom ImDrawList charts on the same token
// contract as regular ImGui controls. Alpha-only variants must not copy RGB
// literals into individual chart implementations.
inline ImVec4 withAlpha(const ImVec4& color, float alpha)
{
    return ImVec4(color.x, color.y, color.z, alpha);
}

inline ImU32 colorU32(const ImVec4& color)
{
    return ImGui::ColorConvertFloat4ToU32(color);
}

inline ImU32 colorU32(const ImVec4& color, float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(withAlpha(color, alpha));
}

}
}

#endif
