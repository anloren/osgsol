#pragma once

static ImVec4 ColorFromBytes(uint8_t r, uint8_t g, uint8_t b)
{ return ImVec4((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f); };

void StyleColorsVisualStudio(ImGuiStyle* dst)
{
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 bgColor           = ColorFromBytes(37, 37, 38);
    const ImVec4 lightBgColor      = ColorFromBytes(82, 82, 85);
    const ImVec4 veryLightBgColor  = ColorFromBytes(90, 90, 95);

    const ImVec4 panelColor        = ColorFromBytes(51, 51, 55);
    const ImVec4 panelHoverColor   = ColorFromBytes(29, 151, 236);
    const ImVec4 panelActiveColor  = ColorFromBytes(0, 119, 200);

    const ImVec4 textColor         = ColorFromBytes(255, 255, 255);
    const ImVec4 textDisabledColor = ColorFromBytes(151, 151, 151);
    const ImVec4 borderColor       = ColorFromBytes(78, 78, 78);

    colors[ImGuiCol_Text]                 = textColor;
    colors[ImGuiCol_TextDisabled]         = textDisabledColor;
    colors[ImGuiCol_TextSelectedBg]       = panelActiveColor;
    colors[ImGuiCol_WindowBg]             = bgColor;
    colors[ImGuiCol_ChildBg]              = bgColor;
    colors[ImGuiCol_PopupBg]              = bgColor;
    colors[ImGuiCol_Border]               = borderColor;
    colors[ImGuiCol_BorderShadow]         = borderColor;
    colors[ImGuiCol_FrameBg]              = panelColor;
    colors[ImGuiCol_FrameBgHovered]       = panelHoverColor;
    colors[ImGuiCol_FrameBgActive]        = panelActiveColor;
    colors[ImGuiCol_TitleBg]              = bgColor;
    colors[ImGuiCol_TitleBgActive]        = bgColor;
    colors[ImGuiCol_TitleBgCollapsed]     = bgColor;
    colors[ImGuiCol_MenuBarBg]            = panelColor;
    colors[ImGuiCol_ScrollbarBg]          = panelColor;
    colors[ImGuiCol_ScrollbarGrab]        = lightBgColor;
    colors[ImGuiCol_ScrollbarGrabHovered] = veryLightBgColor;
    colors[ImGuiCol_ScrollbarGrabActive]  = veryLightBgColor;
    colors[ImGuiCol_CheckMark]            = panelActiveColor;
    colors[ImGuiCol_SliderGrab]           = panelHoverColor;
    colors[ImGuiCol_SliderGrabActive]     = panelActiveColor;
    colors[ImGuiCol_Button]               = panelColor;
    colors[ImGuiCol_ButtonHovered]        = panelHoverColor;
    colors[ImGuiCol_ButtonActive]         = panelHoverColor;
    colors[ImGuiCol_Header]               = panelColor;
    colors[ImGuiCol_HeaderHovered]        = panelHoverColor;
    colors[ImGuiCol_HeaderActive]         = panelActiveColor;
    colors[ImGuiCol_Separator]            = borderColor;
    colors[ImGuiCol_SeparatorHovered]     = borderColor;
    colors[ImGuiCol_SeparatorActive]      = borderColor;
    colors[ImGuiCol_ResizeGrip]           = bgColor;
    colors[ImGuiCol_ResizeGripHovered]    = panelColor;
    colors[ImGuiCol_ResizeGripActive]     = lightBgColor;
    colors[ImGuiCol_PlotLines]            = panelActiveColor;
    colors[ImGuiCol_PlotLinesHovered]     = panelHoverColor;
    colors[ImGuiCol_PlotHistogram]        = panelActiveColor;
    colors[ImGuiCol_PlotHistogramHovered] = panelHoverColor;
    colors[ImGuiCol_DragDropTarget]       = bgColor;
    colors[ImGuiCol_NavHighlight]         = bgColor;
    colors[ImGuiCol_Tab]                  = bgColor;
    colors[ImGuiCol_TabActive]            = panelActiveColor;
    colors[ImGuiCol_TabUnfocused]         = bgColor;
    colors[ImGuiCol_TabUnfocusedActive]   = panelActiveColor;
    colors[ImGuiCol_TabHovered]           = panelHoverColor;

    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.GrabRounding      = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding       = 0.0f;
}

void StyleColorsSonicRiders(ImGuiStyle* dst)
{
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();
    style.FrameRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.GrabRounding = 4.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.73f, 0.75f, 0.74f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.09f, 0.09f, 0.09f, 0.94f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.71f, 0.39f, 0.39f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.84f, 0.66f, 0.66f, 0.40f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.84f, 0.66f, 0.66f, 0.67f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.47f, 0.22f, 0.22f, 0.67f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.47f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.47f, 0.22f, 0.22f, 0.67f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.34f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.71f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.84f, 0.66f, 0.66f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.47f, 0.22f, 0.22f, 0.65f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.71f, 0.39f, 0.39f, 0.65f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
    colors[ImGuiCol_Header]                 = ImVec4(0.71f, 0.39f, 0.39f, 0.54f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.84f, 0.66f, 0.66f, 0.65f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.84f, 0.66f, 0.66f, 0.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.71f, 0.39f, 0.39f, 0.54f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.71f, 0.39f, 0.39f, 0.54f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.71f, 0.39f, 0.39f, 0.54f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.84f, 0.66f, 0.66f, 0.66f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.84f, 0.66f, 0.66f, 0.66f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.71f, 0.39f, 0.39f, 0.54f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.84f, 0.66f, 0.66f, 0.66f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.84f, 0.66f, 0.66f, 0.66f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

void StyleColorsLightBlue(ImGuiStyle* dst)
{
    ImGui::StyleColorsLight();
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();

    style.Colors[ImGuiCol_Text] = ImVec4(0.31f, 0.25f, 0.24f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.74f, 0.74f, 0.94f, 1.00f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.68f, 0.68f, 0.68f, 0.00f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.50f, 0.50f, 0.50f, 0.60f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.62f, 0.70f, 0.72f, 0.56f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.95f, 0.33f, 0.14f, 0.47f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.97f, 0.31f, 0.13f, 0.81f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.42f, 0.75f, 0.61f, 0.53f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.42f, 0.75f, 1.00f, 0.53f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.40f, 0.65f, 0.80f, 0.20f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.40f, 0.62f, 0.80f, 0.15f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.39f, 0.64f, 0.80f, 0.30f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.67f, 0.80f, 0.59f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.25f, 0.48f, 0.53f, 0.67f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.48f, 0.47f, 0.47f, 0.71f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.31f, 0.47f, 0.99f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(1.00f, 0.79f, 0.18f, 0.78f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.42f, 0.82f, 1.00f, 0.81f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.72f, 1.00f, 1.00f, 0.86f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.65f, 0.78f, 0.84f, 0.80f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.75f, 0.88f, 0.94f, 0.80f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.55f, 0.68f, 0.74f, 0.80f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.60f, 0.60f, 0.80f, 0.30f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 0.99f, 0.54f, 0.43f);
    style.Alpha = 1.0f;
    style.FrameRounding = 4;
    style.IndentSpacing = 12.0f;
}

void StyleColorsTransparent(ImGuiStyle* dst)
{
    ImGui::StyleColorsLight();
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();

    style.Colors[ImGuiCol_Text] = ImVec4(0.86f, 0.92f, 0.95f, 1.00f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 1.00f, 0.54f, 0.43f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.15f, 0.18f, 0.70f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.15f, 0.18f, 0.70f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.05f, 0.15f, 0.18f, 0.70f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.25f, 0.28f, 0.70f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.03f, 0.18f, 0.26f, 0.90f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.25f, 0.28f, 0.70f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.45f, 0.25f, 0.28f, 0.70f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.05f, 0.55f, 0.58f, 0.70f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.40f, 0.47f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.50f, 0.57f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.07f, 0.65f, 0.80f, 0.20f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.14f, 0.86f, 0.97f, 0.70f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(1.00f, 0.58f, 0.16f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.00f, 0.88f, 0.16f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.00f, 0.78f, 0.36f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.14f, 0.86f, 0.97f, 0.70f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.58f, 0.16f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.05f, 0.24f, 0.28f, 0.78f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.24f, 0.28f, 0.78f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.05f, 0.44f, 0.48f, 0.78f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.07f, 0.40f, 0.47f, 0.80f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.87f, 0.40f, 0.47f, 0.80f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.07f, 0.60f, 0.67f, 0.80f);
    style.Alpha = 1.0f;
    style.FrameRounding = 4;
    style.IndentSpacing = 12.0f;
}

// v0.15-vision:「任务控制台」主题——深空蓝黑背景 + 单一青蓝强调色,数据点专属配色
// (地震红/灾害橙等)完全独立于本主题,互不影响。取代硬编码的 StyleColorsDark 默认值。
void StyleColorsMissionControl(ImGuiStyle* dst)
{
    ImGuiStyle& style = dst ? *dst : ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 windowBg  = ImVec4(0.063f, 0.094f, 0.137f, 0.92f);   // rgb(16,24,35)
    const ImVec4 panelBg   = ColorFromBytes(22, 33, 46);              // 二级面板色(输入框/按钮闲置态)
    const ImVec4 border    = ImVec4(0.353f, 0.706f, 1.0f, 0.25f);     // 细边框微光(近似;ImGui 无真正光晕)
    const ImVec4 accent    = ColorFromBytes(59, 167, 255);            // #3ba7ff 强调色
    const ImVec4 accentDim = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    const ImVec4 text      = ColorFromBytes(234, 242, 248);
    const ImVec4 textDim   = ImVec4(0.706f, 0.784f, 0.863f, 0.65f);

    colors[ImGuiCol_Text]                 = text;
    colors[ImGuiCol_TextDisabled]         = textDim;
    colors[ImGuiCol_TextSelectedBg]       = accentDim;
    colors[ImGuiCol_WindowBg]             = windowBg;
    colors[ImGuiCol_ChildBg]              = windowBg;
    colors[ImGuiCol_PopupBg]              = windowBg;
    colors[ImGuiCol_Border]               = border;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]              = panelBg;
    colors[ImGuiCol_FrameBgHovered]       = accentDim;
    colors[ImGuiCol_FrameBgActive]        = accent;
    colors[ImGuiCol_TitleBg]              = windowBg;
    colors[ImGuiCol_TitleBgActive]        = panelBg;
    colors[ImGuiCol_TitleBgCollapsed]     = windowBg;
    colors[ImGuiCol_MenuBarBg]            = panelBg;
    colors[ImGuiCol_ScrollbarBg]          = windowBg;
    colors[ImGuiCol_ScrollbarGrab]        = panelBg;
    colors[ImGuiCol_ScrollbarGrabHovered] = accentDim;
    colors[ImGuiCol_ScrollbarGrabActive]  = accent;
    colors[ImGuiCol_CheckMark]            = accent;
    colors[ImGuiCol_SliderGrab]           = accentDim;
    colors[ImGuiCol_SliderGrabActive]     = accent;
    colors[ImGuiCol_Button]               = panelBg;
    colors[ImGuiCol_ButtonHovered]        = accentDim;
    colors[ImGuiCol_ButtonActive]         = accent;
    colors[ImGuiCol_Header]               = panelBg;
    colors[ImGuiCol_HeaderHovered]        = accentDim;
    colors[ImGuiCol_HeaderActive]         = accent;
    colors[ImGuiCol_Separator]            = border;
    colors[ImGuiCol_SeparatorHovered]     = accentDim;
    colors[ImGuiCol_SeparatorActive]      = accent;
    colors[ImGuiCol_ResizeGrip]           = panelBg;
    colors[ImGuiCol_ResizeGripHovered]    = accentDim;
    colors[ImGuiCol_ResizeGripActive]     = accent;
    colors[ImGuiCol_PlotLines]            = accent;
    colors[ImGuiCol_PlotLinesHovered]     = accentDim;
    colors[ImGuiCol_PlotHistogram]        = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accentDim;
    colors[ImGuiCol_Tab]                  = windowBg;
    colors[ImGuiCol_TabActive]            = panelBg;
    colors[ImGuiCol_TabUnfocused]         = windowBg;
    colors[ImGuiCol_TabUnfocusedActive]   = panelBg;
    colors[ImGuiCol_TabHovered]           = accentDim;

    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 6.0f;
    style.GrabRounding      = 6.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding       = 6.0f;
    style.WindowBorderSize  = 1.0f;
}
