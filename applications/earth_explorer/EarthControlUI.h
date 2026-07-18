#ifndef EARTH_CONTROL_UI_H
#define EARTH_CONTROL_UI_H

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <osg/Vec3d>
#include <osgViewer/Viewer>
#include <ui/ImGui.h>
#include <ui/ImGuiComponents.h>
#include <readerwriter/EarthManipulator.h>
#include <pipeline/Utilities.h>
#include <readerwriter/SolarPosition.h>
#include <modeling/Math.h>
#include "LayerManager.h"
#include "marker_style.h"
#include "flight_data.h"
#include "sat_data.h"
#include "ais_data.h"
#include "feed_layer.h"
#include "event_ticker.h"
#include "ui_card.h"
#include "ai_ui.h"
#include "ai_media.h"
#include "overlay_lod_badge.h"
#include "earth_config.h"
#include "earth_control_layout.h"
#include <readerwriter/TileCallback.h>
#if OSGSOL_BUILD_SCIENCE
#include <ScienceQueryService.h>
#include "science_earth_panel.h"
#include "science_preview_layer.h"
#endif

// EarthExplorer 的 ImGui 控制面板。直接驱动 EarthManipulator 与 EarthAtmosphereOcean，
// 不经 USER 事件中转。
struct EarthControlUI : public osgVerse::ImGuiContentHandler
{
    osgVerse::EarthManipulator* _mani;
    osgVerse::EarthAtmosphereOcean* _earth;
    osgViewer::Viewer* _viewer;                      // 用于退出程序
    LayerManager* _layers = nullptr;   // 由 main 注入
    FlightLayer* _flight = nullptr;    // 由 main 注入
    SatelliteLayer* _satellites = nullptr;  // 由 main 注入
    ShipLayer* _ships = nullptr;       // 由 main 注入
    AIChatUI* _aiUI = nullptr;         // 由 main 注入；为空则不画底部聊天条
    earthai::AIChatCore* _aiCore = nullptr;   // 由 main 注入；draw() 内部对空指针安全
    earthai::MediaManager* _aiMedia = nullptr;   // 由 main 注入；为空则📷按钮禁用(见 draw())
#if OSGSOL_BUILD_SCIENCE
    earthscience::ScienceQueryService* _scienceService = nullptr;
    SciencePreviewLayer* _scienceLayer = nullptr;
    ScienceEarthPanel _sciencePanel;
#endif
    float _sunAz, _sunEl;     // 太阳方位角/高度角（度）
    float _exposure;          // HDR 曝光
    bool  _exposureAuto;      // 自适应曝光(随高度):低空高曝光、高空低曝光
    float _globalOpaque;      // 大气强度
    bool  _ocean;             // 海洋开关
    float _gotoLat, _gotoLon, _gotoAltKm;            // 跳转目标
    int   _bookmarkTime;                             // 下一个书签的时间戳（帧）
    bool _alwaysDay, _realTimeSun, _followClock;   // v0.15-vision:_alwaysDay 默认开启的常昼模式
    int  _year, _month, _day; float _utcHour;
    char _layerFilter[64] = "";       // 图层目录搜索框内容(Task 4)
    EventTickerUI _ticker;            // T8:事件流卡 + 顶部状态带(默认关,见 event_ticker.h)
    earthui::CardStack _cardStack;   // v0.15-vision:统一右上角信息卡组件(见 ui_card.h)
    bool _detailBadgeDismissed = false;      // 用户关掉角标
    std::string _detailBadgeLastLayer;       // 上次角标对应的激活层 id(变了则重置 dismissed)
    unsigned int _detailBadgeShownFrame = 0; // 角标本轮首次出现的帧号(Task 4:自消失计时起点)
    bool _detailBadgeStretchActivePrev = false;  // 上一帧是否处于"超缩放拉伸"去抖窗内(边沿检测用)

    // ASCII 大小写折叠(UTF-8 多字节原样保留):搜索过滤大小写不敏感、中文按字节子串匹配。
    static std::string lowered(const std::string& s)
    {
        std::string r = s;
        for (size_t i = 0; i < r.size(); ++i)
            if (r[i] >= 'A' && r[i] <= 'Z') r[i] += 'a' - 'A';
        return r;
    }

    static void panelControlLabel(const char* label)
    {
        ImGui::TextWrapped("%s", label);
        ImGui::SetNextItemWidth(-1.0f);
    }

    static bool panelSliderFloat(const char* label, const char* id, float* value,
                                 float minimum, float maximum, const char* format)
    {
        panelControlLabel(label);
        return ImGui::SliderFloat(id, value, minimum, maximum, format);
    }

    static bool panelSliderInt(const char* label, const char* id, int* value,
                               int minimum, int maximum)
    {
        panelControlLabel(label);
        return ImGui::SliderInt(id, value, minimum, maximum);
    }

    static bool panelInputFloat(const char* label, const char* id, float* value,
                                const char* format)
    {
        panelControlLabel(label);
        return ImGui::InputFloat(id, value, 0.0f, 0.0f, format);
    }

    static bool panelInputInt(const char* label, const char* id, int* value)
    {
        panelControlLabel(label);
        return ImGui::InputInt(id, value);
    }

    static bool panelCheckbox(const char* label, const char* id, bool* value)
    {
        bool changed = ImGui::Checkbox(id, value);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", label);
        if (ImGui::IsItemClicked())
        {
            *value = !*value;
            changed = true;
        }
        return changed;
    }

    EarthControlUI(osgVerse::EarthManipulator* m, osgVerse::EarthAtmosphereOcean* e, osgViewer::Viewer* v)
        : _mani(m), _earth(e), _viewer(v), _sunAz(0.0f), _sunEl(0.0f), _exposure(0.25f), _exposureAuto(true), _globalOpaque(1.0f), _ocean(true)
        , _gotoLat(35.36f), _gotoLon(138.73f), _gotoAltKm(150.0f), _bookmarkTime(0)   // 同 fly_to 默认高度(v0.15-vision)
        , _alwaysDay(true), _realTimeSun(false), _followClock(true), _year(2024), _month(6), _day(21), _utcHour(18.0f) {}

    void updateSun()
    {
        float az = osg::DegreesToRadians(_sunAz), el = osg::DegreesToRadians(_sunEl);
        osg::Vec3 dir(cosf(el) * cosf(az), cosf(el) * sinf(az), sinf(el));
        _earth->commonUniforms["WorldSunDir"]->set(dir);
    }

    void applyRealtimeSun()
    {
        int Y=_year, Mo=_month, D=_day; double H=_utcHour;
        if (_followClock)
        {
            time_t t = time(NULL); struct tm g; gmtime_r(&t, &g);
            Y=g.tm_year+1900; Mo=g.tm_mon+1; D=g.tm_mday;
            H = g.tm_hour + g.tm_min/60.0 + g.tm_sec/3600.0;
        }
        osgVerse::SubsolarPoint s = osgVerse::computeSubsolarPoint(Y, Mo, D, H);
        osg::Vec3d ecef = osgVerse::Coordinate::convertLLAtoECEF(
            osg::Vec3d(s.declRad, s.lonRad, 6.371e6));
        ecef.normalize();
        _earth->commonUniforms["WorldSunDir"]->set(osg::Vec3(ecef));
    }

    // 默认「常昼」模式(v0.15-vision):每帧把太阳对准当前相机位置(公式与"太阳对准相机"
    // 按钮一致),不管 AI 飞去哪里还是手动拖转到哪,画面里永远是白天。与真实时间太阳互斥
    // (谁最后被勾选谁生效,见 runInternal() 的 if/else 与两个 checkbox 的联动)。
    void applyAlwaysDaySun()
    {
        osg::Vec3d lla = _mani->computeEyeLatLonHeight();
        _sunAz = (float)osg::RadiansToDegrees(lla[1]);
        _sunEl = (float)osg::RadiansToDegrees(lla[0]);
        updateSun();
    }

    // 随高度自适应曝光:低空 inscatter 弱、画面塌暗→高曝光;高空 inscatter 强、本就亮→低曝光防过曝。
    // 只设 HdrExposure uniform,着色器逻辑不动。阈值/值用 env 暴露便于不重编调:
    // EARTH_EXP_LO / EARTH_EXP_HI(低/高空曝光)、EARTH_EXP_ALTLO / EARTH_EXP_ALTHI(过渡高度,km)。
    float adaptiveExposure(double altM) const
    {
        static const float expLo = []{ const char* e = getenv("EARTH_EXP_LO"); return e ? (float)atof(e) : 1.0f; }();
        static const float expHi = []{ const char* e = getenv("EARTH_EXP_HI"); return e ? (float)atof(e) : 0.25f; }();
        static const double altLo = []{ const char* e = getenv("EARTH_EXP_ALTLO"); return e ? atof(e) * 1000.0 : 80000.0; }();
        static const double altHi = []{ const char* e = getenv("EARTH_EXP_ALTHI"); return e ? atof(e) * 1000.0 : 4000000.0; }();
        double t = (altM - altLo) / (altHi - altLo);
        if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
        t = t * t * (3.0 - 2.0 * t);  // smoothstep
        return (float)(expLo * (1.0 - t) + expHi * t);
    }

    virtual void runInternal(osgVerse::ImGuiManager* mgr)
    {
        ImFont* font = ImGuiFonts.count("LXGWFasmartGothic") ? ImGuiFonts["LXGWFasmartGothic"] : NULL;
        if (font) ImGui::PushFont(font);
        if (_realTimeSun) applyRealtimeSun();
        else if (_alwaysDay) applyAlwaysDaySun();
        if (_exposureAuto)
        {
            _exposure = adaptiveExposure(_mani->computeEyeLatLonHeight()[2]);  // 米
            _earth->commonUniforms["HdrExposure"]->set(_exposure);
        }

        // 用户反馈 1(HUD 干净截图):MediaManager 抓帧期间(hudHide()~hudRestore() 之间)
        // 整段跳过下面所有 ImGui 窗口内容——不产生任何 ImGui::Begin/绘制调用,ImGui 本帧
        // draw data 为空,ImGuiRenderCallback::operator() 的 Render() 自然什么像素都不画,
        // 截到的最终合成帧(finalCamera 完全正常参与合成,见 ai_media.h 构造函数注释的
        // 踩坑记录)就是"没有 Earth Control 面板/对话条/详情卡"的干净画面。上面的太阳/曝光
        // 状态更新不受影响(它们驱动场景着色 uniform,不是 ImGui 可视内容,抓帧时也应该
        // 正常生效)。一帧闪烁("快门")是预期行为,已在设计里确认。
        bool hudHidden = _aiMedia && _aiMedia->isHudHidden();
        if (!hudHidden)
        {
        ImGuiIO& io = ImGui::GetIO();
        const earthui::EarthControlPanelLayout panelLayout =
            earthui::computeEarthControlPanelLayout(io.DisplaySize.x, io.DisplaySize.y);
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2(panelLayout.defaultWidth, panelLayout.defaultHeight),
            ImGuiCond_FirstUseEver);
        // Every frame constraints also repair an oversized value already saved in imgui.ini.
        // Standard ImGui scrolling remains enabled so expanded sections never grow over Earth.
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(panelLayout.minWidth, panelLayout.minHeight),
            ImVec2(panelLayout.maxWidth, panelLayout.maxHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 14.0f);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,
                              ImVec4(0.23f, 0.65f, 1.0f, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                              ImVec4(0.23f, 0.65f, 1.0f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,
                              ImVec4(0.23f, 0.65f, 1.0f, 1.0f));
        if (ImGui::Begin("Earth Control / 地球控制台", nullptr,
                         ImGuiWindowFlags_AlwaysVerticalScrollbar))
        {
            ImGui::PushTextWrapPos(0.0f);
            // ---- 相机读数 ----
            if (ImGui::CollapsingHeader(u8"相机 Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                osg::Vec3d lla = _mani->computeEyeLatLonHeight();  // 弧度, 弧度, 米
                ImGui::Text(u8"纬度 Lat: %.5f", osg::RadiansToDegrees(lla[0]));
                ImGui::Text(u8"经度 Lon: %.5f", osg::RadiansToDegrees(lla[1]));
                ImGui::Text(u8"高度 Alt: %.1f km", lla[2] / 1000.0);
                if (ImGui::Button(u8"回到全球视角 Home")) _mani->home(0.0);
            }

            // ---- 太阳/光照 ----
            if (ImGui::CollapsingHeader(u8"太阳 Sun", ImGuiTreeNodeFlags_DefaultOpen))
            {
                // v0.15-vision:常昼模式默认开启;开启时手动滑块/按钮禁用(同 _exposureAuto
                // 禁用曝光滑块的既有惯例,见下方"渲染"节),与真实时间太阳互斥。
                if (panelCheckbox(u8"常昼 Always Day", "##always_day", &_alwaysDay))
                { if (_alwaysDay) _realTimeSun = false; }
                if (_alwaysDay) ImGui::BeginDisabled();
                if (panelSliderFloat(u8"方位角 Azimuth", "##sun_azimuth",
                                     &_sunAz, -180.0f, 180.0f, "%.0f"))
                    updateSun();
                if (panelSliderFloat(u8"高度角 Elevation", "##sun_elevation",
                                     &_sunEl, -90.0f, 90.0f, "%.0f"))
                    updateSun();
                if (ImGui::Button(u8"太阳对准相机")) {
                    osg::Vec3d lla = _mani->computeEyeLatLonHeight();
                    _sunAz = (float)osg::RadiansToDegrees(lla[1]);
                    _sunEl = (float)osg::RadiansToDegrees(lla[0]);
                    updateSun();
                }
                if (_alwaysDay) ImGui::EndDisabled();
                ImGui::Separator();
                if (panelCheckbox(u8"真实时间太阳 Real-time", "##realtime_sun",
                                  &_realTimeSun))
                { if (_realTimeSun) { _alwaysDay = false; applyRealtimeSun(); } }
                if (_realTimeSun)
                {
                    panelCheckbox(u8"跟随系统时钟", "##follow_clock", &_followClock);
                    if (!_followClock)
                    {
                        panelInputInt(u8"年 Year", "##sun_year", &_year);
                        panelInputInt(u8"月 Month", "##sun_month", &_month);
                        panelInputInt(u8"日 Day", "##sun_day", &_day);
                        panelSliderFloat(u8"UTC 小时", "##utc_hour",
                                         &_utcHour, 0.0f, 24.0f, "%.1f");
                    }
                }
            }

            // ---- 渲染 ----
            if (ImGui::CollapsingHeader(u8"渲染 Render", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (panelCheckbox(u8"海洋 Ocean", "##ocean", &_ocean))
                    _earth->commonUniforms["OceanOpaque"]->set(_ocean ? 1.0f : 0.0f);
                panelCheckbox(u8"自适应曝光 Auto", "##exposure_auto", &_exposureAuto);
                if (_exposureAuto) ImGui::BeginDisabled();
                if (panelSliderFloat(u8"曝光 Exposure", "##exposure",
                                     &_exposure, 0.05f, 1.5f, "%.2f"))
                    _earth->commonUniforms["HdrExposure"]->set(_exposure);
                if (_exposureAuto) ImGui::EndDisabled();
                if (panelSliderFloat(u8"大气强度 Atmosphere", "##atmosphere",
                                     &_globalOpaque, 0.0f, 1.0f, "%.2f"))
                    _earth->commonUniforms["GlobalOpaque"]->set(_globalOpaque);
            }
            // ---- 图层 ----
            if (_layers && ImGui::CollapsingHeader(u8"图层 Layers", ImGuiTreeNodeFlags_DefaultOpen))
            {
                // 预设按钮行:一键切换场景图层组合(earth_main 注册,见 LayerManager::applyPreset)
                const std::vector<Preset> presets = _layers->presetsSnapshot();
                for (size_t p = 0; p < presets.size(); ++p)
                {
                    if (p > 0) ImGui::SameLine();
                    if (ImGui::SmallButton(presets[p].name.c_str()))
                        _layers->applyPreset(presets[p].name);
                }
                // T8:事件流卡/状态带开关(渲染在右上角/顶部,这里只是操作区里的开关按钮)
                if (!presets.empty()) ImGui::SameLine();
                if (ImGui::SmallButton(u8"事件流")) _ticker.showTicker = !_ticker.showTicker;
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"状态带")) _ticker.showStatusBar = !_ticker.showStatusBar;
                // 搜索框:按 displayName/group 过滤(大小写不敏感;中文直接字节子串匹配)
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(u8"##图层搜索", u8"搜索图层 Filter...",
                                         _layerFilter, sizeof(_layerFilter));
                std::string filter = lowered(_layerFilter);

                // 渲染前按 group 归并(组按首次出现顺序,组内按注册顺序)——同一 group 的层
                // 注册不连续时(实际发生:"实时数据 / Live" 分两处注册)不再重复分组标题。
                const std::vector<OverlayLayer> ls = _layers->layersSnapshot();
                std::vector<std::string> groupNames;
                std::vector<std::vector<size_t> > groupItems;   // 存 layers() 原始下标
                for (size_t i = 0; i < ls.size(); ++i)
                {
                    size_t g = 0;
                    for (; g < groupNames.size(); ++g) if (groupNames[g] == ls[i].group) break;
                    if (g == groupNames.size())
                    { groupNames.push_back(ls[i].group); groupItems.push_back(std::vector<size_t>()); }
                    groupItems[g].push_back(i);
                }
                for (size_t g = 0; g < groupNames.size(); ++g)
                {
                    // 过滤:组名命中 → 整组可见;否则只留 displayName 命中的层,组内无匹配则整组隐藏
                    bool groupHit = !filter.empty() &&
                                    lowered(groupNames[g]).find(filter) != std::string::npos;
                    std::vector<size_t> visible;
                    for (size_t k = 0; k < groupItems[g].size(); ++k)
                    {
                        size_t idx = groupItems[g][k];
                        if (filter.empty() || groupHit ||
                            lowered(ls[idx].displayName).find(filter) != std::string::npos)
                            visible.push_back(idx);
                    }
                    if (visible.empty()) continue;
                    if (!filter.empty()) ImGui::SetNextItemOpen(true);   // 含匹配层的组自动展开
                    if (!ImGui::TreeNodeEx(groupNames[g].c_str(),
                            (groupNames[g] == u8"底图 / 标注") ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                        continue;
                    for (size_t k = 0; k < visible.size(); ++k)
                    {
                        const OverlayLayer& l = ls[visible[k]];
                        ImGui::PushID((int)visible[k]);   // 用注册下标,跨组全局唯一
                        // needsKey（缺 key）或无 apply 回调（如常开底图）的层置灰、不可交互
                        bool inactive = l.needsKey || !l.apply;
                        if (inactive) ImGui::BeginDisabled();
                        bool en = l.enabled;
                        {
                            ImVec2 cur = ImGui::GetCursorScreenPos();
                            float ic = ImGui::GetTextLineHeight();
                            ImU32 icol = ImGui::ColorConvertFloat4ToU32(
                                ImVec4(l.iconColor.x(), l.iconColor.y(), l.iconColor.z(), 1.0f));
                            earthmark::drawMarkerIcon(ImGui::GetWindowDrawList(), l.shape,
                                cur.x + ic*0.5f, cur.y + ic*0.5f, ic, icol);
                            ImGui::Dummy(ImVec2(ic + 4.0f, ic)); ImGui::SameLine();
                        }
                        if (panelCheckbox(l.displayName.c_str(), "##enabled", &en))
                            _layers->setEnabled(l.id, en);
                        if (l.needsKey) ImGui::TextDisabled(u8"需要密钥 🔑");
                        if (!l.subtitle.empty()) ImGui::TextDisabled("%s", l.subtitle.c_str());
                        // 任务A:抓取失败 UI 可见——只在层已开启且接了 fetchStatus 回调时查询
                        // (底图/标注等常开层没有"抓取"概念,回调为空,天然跳过)。
                        // 0=未抓/idle(刚开启还没等到第一轮结果)→"加载中…";
                        // 2=失败(如上游 404 死链)→ 醒目警示色 + 悬浮 tooltip 显示原因;
                        // 1=成功不额外提示,维持既有面板简洁;
                        // 3=成功但 0 要素(如当前无活跃飓风)→"· 当前无数据"(Task 1,中性灰字,
                        // 与"失败"/"加载中"区分,避免"抓取成功但暂无要素"被误判为源坏了)。
                        if (l.enabled && l.fetchStatus)
                        {
                            std::string fetchErr;
                            int fetchSt = l.fetchStatus(fetchErr);
                            if (fetchSt == 2)
                            {
                                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), u8"⚠ 抓取失败");
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fetchErr.c_str());
                            }
                            else if (fetchSt == 0)
                            {
                                ImGui::TextDisabled(u8"加载中…");
                            }
                            else if (fetchSt == 3)
                            {
                                ImGui::TextDisabled(u8"· 当前无数据");
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"抓取成功,当前无数据(非故障)");
                            }
                        }
                        if (l.hasOpacity && l.enabled)
                        {
                            float op = l.opacity;
                            std::string label = l.displayName + u8" 透明度";
                            if (panelSliderFloat(label.c_str(), "##opacity", &op,
                                                 0.0f, 1.0f, "%.2f"))
                                _layers->setOpacity(l.id, op);
                        }
                        if (inactive) ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
            // 地震详情等「信息呈现」UI 不在此操作面板内,统一放右上角独立面板(见 End() 之后)。

#if OSGSOL_BUILD_SCIENCE
            _sciencePanel.drawOperations(
                _scienceService, _scienceLayer, _layers, _mani);
#endif

            // ---- 跳转 ----
            if (ImGui::CollapsingHeader(u8"跳转 Go To", ImGuiTreeNodeFlags_DefaultOpen))
            {
                panelInputFloat(u8"纬度 Lat", "##goto_lat", &_gotoLat, "%.4f");
                panelInputFloat(u8"经度 Lon", "##goto_lon", &_gotoLon, "%.4f");
                panelInputFloat(u8"高度 km", "##goto_altitude", &_gotoAltKm, "%.1f");
                if (ImGui::Button(u8"飞过去 Go"))
                {
                    _mani->setByEye(osg::DegreesToRadians((double)_gotoLat),
                                    osg::DegreesToRadians((double)_gotoLon),
                                    (double)_gotoAltKm * 1000.0);
                }
            }

            // ---- 书签/巡游 ----
            if (ImGui::CollapsingHeader(u8"书签 Bookmarks"))
            {
                if (ImGui::Button(u8"记录当前视角 Save"))
                {
                    _mani->insertControlPointFromCurrentView((float)_bookmarkTime);
                    _bookmarkTime += 60;
                }
                ImGui::SameLine();
                ImGui::Text(u8"已存 %d", (int)_mani->getControlPoints().size());
                if (ImGui::Button(u8"播放巡游 Play"))
                {
                    if (!_mani->getControlPoints().empty()) _mani->startAnimation();
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"停止 Stop")) _mani->stopAnimation(false);
                if (ImGui::Button(u8"清空 Clear")) { _mani->clearControlPoints(); _bookmarkTime = 0; }
            }

            // ---- 设置(Task 5:earthcfg 可视化调节 + 恢复默认) ----
            // 注:earthcfg::params() 返回 std::deque<Param>&(非 vector——Param 内含
            // std::atomic<double>,不可拷贝/移动，见 earth_config.h 注释);deque 同样
            // 支持 operator[]/size()，遍历逻辑与 vector 一致，仅类型名不同。
            if (ImGui::CollapsingHeader(u8"设置 Settings"))
            {
                std::deque<earthcfg::Param>& ps = earthcfg::params();
                std::string curGroup;
                for (size_t i = 0; i < ps.size(); ++i)
                {
                    earthcfg::Param& p = ps[i];
                    if (p.group != curGroup) { curGroup = p.group; ImGui::SeparatorText(p.group.c_str()); }
                    ImGui::PushID((int)i);
                    if (p.kind == earthcfg::PK_INT) {
                        int v = (int)p.value.load();
                        if (panelSliderInt(p.label.c_str(), "##value", &v,
                                           (int)p.minVal, (int)p.maxVal))
                            earthcfg::setValue(p.id, (double)v);
                    } else if (p.kind == earthcfg::PK_DOUBLE) {
                        float v = (float)p.value.load();
                        if (panelSliderFloat(p.label.c_str(), "##value", &v,
                                             (float)p.minVal, (float)p.maxVal, "%.1f"))
                            earthcfg::setValue(p.id, (double)v);
                    } else {
                        bool v = p.value.load() != 0.0;
                        if (panelCheckbox(p.label.c_str(), "##value", &v))
                            earthcfg::setValue(p.id, v ? 1.0 : 0.0);
                    }
                    if (ImGui::SmallButton(u8"默认")) earthcfg::resetDefault(p.id);
                    ImGui::PopID();
                    ImGui::Spacing();
                }
            }

            // ---- 退出 ----
            ImGui::Separator();
            if (ImGui::Button(u8"退出程序 Quit", ImVec2(-1.0f, 0.0f)))
                if (_viewer) _viewer->setDone(true);
            ImGui::PopTextWrapPos();
        }
#if OSGSOL_BUILD_SCIENCE
        earthui::finishLeftThenDrawScienceResults(
            []() { ImGui::End(); },
            [this]()
            {
                _sciencePanel.drawResults(
                    _scienceService, _scienceLayer, _layers);
            });
#else
        ImGui::End();
#endif
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        // ===== 信息呈现面板:统一锚定右上角,与左上角操作面板分离;可关闭 =====
        // 约定(用户偏好):今后所有"呈现信息"的 UI 都放这里(右上角),不要混进上面的操作面板。
        // 地震详情卡(T2 起)已迁移到下方"通用要素详情"卡(FeedLayer 框架统一提供),
        // 不再有专属 _quake 指针/专属卡片——语义(震级/深度/位置/时间/USGS 链接)完全保留,
        // 只是渲染路径换成了 FeedSelection.title/detail/url。
        if (_flight)
        {
            FlightInfo fi = _flight->getSelected();
            if (fi.valid)
            {
                earthui::Card card;
                card.id = "flight_detail";
                card.style.chipLabel = u8"航班";
                card.style.shape = earthmark::MarkerShape::Arrow;
                card.style.accentColor = osg::Vec4(0.302f, 0.659f, 1.0f, 1.f);
                card.title = u8"航班详情 Flight";
                card.drawBody = [fi]() {
                    ImGui::Text(u8"呼号 Callsign: %s", fi.callsign.c_str());
                    ImGui::Text(u8"国家 Country: %s", fi.country.c_str());
                    ImGui::Text(u8"高度 Alt: %.1f km", fi.altM / 1000.0);
                    ImGui::Text(u8"速度 Speed: %.0f km/h", fi.velMS * 3.6);
                    ImGui::Text(u8"航向 Heading: %.0f°", fi.headingDeg);
                    ImGui::Text(u8"经纬 LatLon: %.3f, %.3f", fi.lat, fi.lon);
                };
                card.onClose = [this]() { _flight->clearSelected(); };
                _cardStack.upsert(card);
            }
        }
        if (_ships)
        {
            ShipInfo si = _ships->getSelected();
            if (si.valid)
            {
                earthui::Card card;
                card.id = "ship_detail";
                card.style.chipLabel = u8"船舶";
                card.style.shape = earthmark::MarkerShape::Ship;
                card.style.accentColor = osg::Vec4(0.208f, 0.878f, 0.816f, 1.f);
                card.title = u8"船舶详情 Ship";
                card.drawBody = [si]() {
                    ImGui::Text(u8"船名 Name: %s", si.name.empty() ? "?" : si.name.c_str());
                    ImGui::Text("MMSI: %lld", si.mmsi);
                    ImGui::Text(u8"航速 SOG: %.1f kn", si.sogKn);
                    ImGui::Text(u8"航向 COG: %.0f°", si.cogDeg);
                    ImGui::Text(u8"经纬 LatLon: %.3f, %.3f", si.lat, si.lon);
                    ImGui::Text(u8"数据龄 Age: %.0f s", si.ageSec);
                };
                card.onClose = [this]() { _ships->clearSelected(); };
                _cardStack.upsert(card);
            }
        }
        if (_satellites)
        {
            SatelliteInfo si = _satellites->getSelected();
            if (si.valid)
            {
                earthui::Card card;
                card.id = "satellite_detail";
                card.style.chipLabel = u8"卫星";
                card.style.shape = earthmark::MarkerShape::SatBox;
                card.style.accentColor = osg::Vec4(1.0f, 0.824f, 0.227f, 1.f);
                card.title = u8"卫星详情 Satellite";
                card.drawBody = [si]() {
                    const char* catName = "?";
                    switch (si.category)
                    {
                    case SatCategory::Station:    catName = u8"空间站"; break;
                    case SatCategory::Navigation: catName = u8"导航星座"; break;
                    case SatCategory::Weather:    catName = u8"气象卫星"; break;
                    default:                      catName = u8"Starlink"; break;
                    }
                    ImGui::Text(u8"名称 Name: %s", si.name.c_str());
                    ImGui::Text(u8"NORAD ID: %d", si.noradId);
                    ImGui::Text(u8"类目 Category: %s", catName);
                    ImGui::Text(u8"高度 Alt: %.1f km", si.altKm);
                    ImGui::Text(u8"速度 Speed: %.2f km/s", si.speedKmS);
                    ImGui::Text(u8"经纬 LatLon: %.3f, %.3f", si.latDeg, si.lonDeg);
                };
                card.onClose = [this]() { _satellites->clearSelected(); };
                _cardStack.upsert(card);
            }
        }
        // 通用"要素详情"卡:全部 FeedLayer 数据源(GDACS 等)共用同一个卡位,不必像
        // 地震/航班那样每个数据源各接一根专属指针。
        {
            earthfeed::FeedSelection fs = earthfeed::currentFeedSelection();
            if (fs.valid)
            {
                earthui::Card card;
                card.id = "feed_detail";
                card.style.chipLabel = u8"要素";
                { earthmark::MarkerVisual mv = earthmark::visualForLayer(fs.sourceId);
                  card.style.shape = mv.shape; card.style.accentColor = mv.color; }
                card.title = u8"要素详情";
                card.drawBody = [fs, this]() {
                    ImGui::Text("%s", fs.title.c_str());
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", fs.detail.c_str());
                    // T2 迁移地震层新增:详情卡带链接(如 USGS 事件页)时多画一个"打开"按钮。
                    // FeedSelection.url 为空的数据源(如 GDACS)不受影响,按钮不出现。
                    if (!fs.url.empty())
                    {
                        ImGui::Separator();
                        if (ImGui::Button(u8"打开 Open"))
                        {
                            // url 来自 feed 网络响应(不可信):剥单引号防 shell 逃逸 + 限定 http(s) 协议
                            std::string safeUrl = fs.url;
                            safeUrl.erase(std::remove(safeUrl.begin(), safeUrl.end(), '\''), safeUrl.end());
                            bool okProto = safeUrl.compare(0, 7, "http://") == 0 || safeUrl.compare(0, 8, "https://") == 0;
                            std::string cmd = okProto ? ("open '" + safeUrl + "'") : std::string();
                            int rc = cmd.empty() ? -1 : system(cmd.c_str());
                            if (rc != 0)
                                OSG_WARN << "[Feed] open url failed/blocked, rc=" << rc
                                         << " url=" << fs.url << std::endl;
                        }
                        // 新闻摘要:交给 AI 去抓正文并分析,摘要出现在底部对话条。
                        // 仅 GDELT 新闻源(sourceId=="gdelt")显示——feed_detail 卡是所有 FeedLayer
                        // 源共用,不门控会泄漏到地震/飓风/GPS 干扰等非新闻卡且 url 未必是文章。
                        // _aiCore 为 null(无 AI 构建/未注入)时同样不出现,零干扰。
                        if (_aiCore && fs.sourceId == "gdelt")
                        {
                            ImGui::SameLine();
                            if (ImGui::Button(u8"AI 摘要"))
                                _aiCore->submit(u8"请总结这条新闻热点(地点:" + fs.title +
                                    u8")的主要内容并分析其重要性。文章链接:" + fs.url +
                                    u8" —— 请先调用 get_news_content 抓取正文再总结。");
                        }
                    }
                };
                card.onClose = []() { earthfeed::clearFeedSelection(); };
                _cardStack.upsert(card);
            }
        }

        // T8:右上角事件流卡 + 顶部微状态带(默认关;EARTH_TICKER/EARTH_STATUSBAR 启动即开)。
        // 与其它信息卡一样归在 hudHidden 保护内——抓帧时不入镜。
        _ticker.registerCards(_cardStack, _mani);
        _ticker.drawStatusBar(_layers);   // 状态带不是卡片(顶部居中、不可关闭),不进 CardStack

        // ===== 底部 AI 对话条:独立浮窗,底部居中锚定,不与左上角/右上角面板重叠 =====
        if (_aiUI) _aiUI->draw(_aiCore, _aiMedia, _mani, _cardStack);

        // v0.15-vision:统一绘制本帧登记的全部信息卡(航班/要素详情/事件流/AI 图表等)。
        // 必须在上面所有 registerCards/upsert 调用之后、本帧只调用一次。
        _cardStack.draw();

        // 超缩放"已达最大细节"角标:引擎在做 OVERLAY 超缩放拉伸时打帧戳,这里带去抖读取。
        if (_layers && _viewer && _viewer->getFrameStamp())
        {
            unsigned int curFrame = (unsigned int)_viewer->getFrameStamp()->getFrameNumber();
            unsigned int lastStretch = osgVerse::TileManager::instance()->getLastOverlayStretchFrame();
            // 找当前激活且带 maxDetailNote 的叠加层(5 层互斥,至多一个)
            const OverlayLayer* active = nullptr;
            const std::vector<OverlayLayer> ls = _layers->layersSnapshot();
            for (size_t i = 0; i < ls.size(); ++i)
                if (ls[i].enabled && !ls[i].maxDetailNote.empty()) { active = &ls[i]; break; }
            // 激活层变了 → 重置关闭态 + 自消失计时(新层的角标该重新出现)
            std::string activeId = active ? active->id : std::string();
            if (activeId != _detailBadgeLastLayer)
            {
                _detailBadgeDismissed = false;
                _detailBadgeLastLayer = activeId;
                _detailBadgeShownFrame = 0;
                _detailBadgeStretchActivePrev = false;
            }

            const unsigned int kDebounceFrames = 30;  // ~0.5s@60fps,防边界抖动
            // 原始"超缩放拉伸中"条件(不含自消失):近期(去抖窗内)确有拉伸 且 有激活层文案。
            bool rawStretchActive = (active != nullptr) && lastStretch != 0u && curFrame >= lastStretch &&
                                     (curFrame - lastStretch) < kDebounceFrames;
            if (rawStretchActive)
            {
                // 刚进入超缩放(上一帧还不是拉伸态)→ 记起点帧号,开始本轮 5s 自消失计时。
                if (!_detailBadgeStretchActivePrev) _detailBadgeShownFrame = curFrame;
            }
            else
            {
                // 离开超缩放(去抖窗已过,~0.5s 内确认已不再拉伸)→ 清零,允许下次重新提示。
                _detailBadgeShownFrame = 0;
            }
            _detailBadgeStretchActivePrev = rawStretchActive;

            unsigned int shownFrames = (curFrame >= _detailBadgeShownFrame) ? (curFrame - _detailBadgeShownFrame) : 0u;
            unsigned int autoDismissFrames = (unsigned int)(earthcfg::getDouble("badge.seconds") * 60.0);
            if (earthui::overlayMaxDetailBadgeVisible(curFrame, lastStretch, kDebounceFrames,
                                                      active != nullptr, _detailBadgeDismissed,
                                                      shownFrames, autoDismissFrames))
            {
                ImGuiIO& io2 = ImGui::GetIO();
                ImGui::SetNextWindowPos(ImVec2(io2.DisplaySize.x - 12.0f, 12.0f),
                                        ImGuiCond_Always, ImVec2(1.0f, 0.0f));  // 右上角锚定
                ImGui::SetNextWindowBgAlpha(0.85f);
                bool open = true;
                if (ImGui::Begin(u8"##max_detail_badge", &open,
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing))
                {
                    std::string text = active->displayName + u8" · 已达最大细节 (" +
                                        active->maxDetailNote + u8")";
                    if (active->opaque) text += u8" · 关闭图层可看地形";
                    ImGui::TextUnformatted(text.c_str());
                }
                ImGui::End();
                if (!open) _detailBadgeDismissed = true;   // 用户点了 [x]
            }
        }
        }   // !hudHidden

        if (font) ImGui::PopFont();
    }
};

#endif
