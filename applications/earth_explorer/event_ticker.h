#ifndef EARTH_EVENT_TICKER_H
#define EARTH_EVENT_TICKER_H

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <osg/Notify>
#include <ui/ImGuiComponents.h>
#include <readerwriter/EarthManipulator.h>
#include "LayerManager.h"
#include "feed_layer.h"
#include "ui_card.h"

// T8:事件流卡(右上信息 UI)+ 顶部微状态带。header-only 仿 ai_cards 模式,
// EarthControlUI 只持有一个实例并在 runInternal 里调 registerCards()/drawStatusBar()。
// UI 分区铁律(用户长期偏好):
//   - 事件流卡 = 信息呈现 → 右上角、独立 ImGui::Begin、标题栏 [x] 可关;
//   - 状态带 = 全局状态(非信息卡)→ 屏幕顶部居中细条,NoInputs 不抢输入;
//   - 两者默认关,开关按钮在 EarthControlUI 图层节预设按钮行旁;
//   - EARTH_TICKER=1 / EARTH_STATUSBAR=1 启动即开(headless 日志断言钩子,
//     同时开启 [Ticker]/[Status] 日志;未设置 = 零影响、零日志)。
struct EventTickerUI
{
    bool showTicker = false;      // 事件流卡显隐(按钮/[x]/EARTH_TICKER 三处驱动)
    bool showStatusBar = false;   // 状态带显隐(按钮/EARTH_STATUSBAR 驱动)

    // 日志断言状态(仅 env 钩子开启时打印,普通 UI 使用零日志)
    bool _logTicker = false, _logStatus = false;
    size_t _lastLoggedCount = (size_t)-1;   // -1 哨兵:首帧(含 0 条)也打印一次
    std::string _lastLoggedNewest;
    int _statusFrame = 0;

    EventTickerUI()
    {
        const char* t = getenv("EARTH_TICKER");
        if (t && *t && std::string(t) != "0") { showTicker = true; _logTicker = true; }
        const char* s = getenv("EARTH_STATUSBAR");
        if (s && *s && std::string(s) != "0") { showStatusBar = true; _logStatus = true; }
    }

    // 相对时间:"刚刚 / Xm 前 / Xh 前 / Xd 前"(事件流行首;中文语境,单位缩写保持紧凑)
    static std::string relativeTime(double unixTime)
    {
        double d = (double)time(nullptr) - unixTime;
        if (d < 0.0) d = 0.0;
        char buf[32];
        if (d < 60.0) snprintf(buf, sizeof(buf), u8"刚刚");
        else if (d < 3600.0) snprintf(buf, sizeof(buf), u8"%dm 前", (int)(d / 60.0));
        else if (d < 86400.0) snprintf(buf, sizeof(buf), u8"%dh 前", (int)(d / 3600.0));
        else snprintf(buf, sizeof(buf), u8"%dd 前", (int)(d / 86400.0));
        return std::string(buf);
    }

    // v0.15-vision:堆叠定位/表头绘制统一交给 earthui::CardStack,本函数只负责构造
    // 一张 Card(正文是事件列表)登记进去。日志断言逻辑(仅 EARTH_TICKER=1 时生效)
    // 不受影响,照旧只在内容变化时打印一次。
    void registerCards(earthui::CardStack& stack, osgVerse::EarthManipulator* mani)
    {
        if (!showTicker) return;
        std::vector<earthfeed::TickerEvent> evs = earthfeed::collectRecentEvents(20);
        if (_logTicker && (evs.size() != _lastLoggedCount
                           || (evs.empty() ? std::string() : evs[0].title) != _lastLoggedNewest))
        {
            _lastLoggedCount = evs.size();
            _lastLoggedNewest = evs.empty() ? std::string() : evs[0].title;
            OSG_NOTICE << "[Ticker] showing " << evs.size() << " events, newest="
                       << _lastLoggedNewest << std::endl;
        }

        earthui::Card card;
        card.id = "ticker";
        card.style.chipLabel = u8"事件流";
        card.title = u8"事件流 / Events";
        // evs 按值捕获(拷贝):它是本函数的局部临时量(每帧现算),函数返回后就析构,
        // CardStack::draw() 真正调用这个 lambda 是在本帧稍后——若按引用捕获会是悬空引用。
        // 与上面 ai_cards.cpp 对 AICard& c 的按引用捕获不同:c 指向持久成员容器的元素。
        card.drawBody = [evs, mani]() {
            if (evs.empty())
            {
                ImGui::TextDisabled(u8"暂无带时间戳的事件");
                ImGui::TextDisabled(u8"(开启地震/EONET 等实时图层后出现)");
            }
            for (size_t i = 0; i < evs.size(); ++i)
            {
                const earthfeed::TickerEvent& e = evs[i];
                std::string label = relativeTime(e.unixTime) + "  [" + e.sourceId + "] "
                                  + e.title + "##ev" + std::to_string((long long)i);
                if (ImGui::Selectable(label.c_str()) && mani)
                {
                    mani->setByEye(osg::DegreesToRadians(e.lat),
                                   osg::DegreesToRadians(e.lon), 300.0 * 1000.0);
                }
            }
        };
        card.onClose = [this]() { showTicker = false; };
        stack.upsert(card);
    }

    // 顶部微状态带:UTC 时钟 · 数据源健康 n/m · 当前预设名。顶部居中细条,
    // NoInputs 完全不抢输入(纯展示,未来加交互再放开)。
    void drawStatusBar(LayerManager* layers)
    {
        if (!showStatusBar) return;
        int okN = 0, enN = 0;
        earthfeed::feedHealth(okN, enN);
        std::string preset = layers ? layers->lastAppliedPreset() : std::string();
        if (preset.empty()) preset = "-";   // 未应用过预设

        time_t t = time(nullptr); struct tm g; gmtime_r(&t, &g);
        char clock[16];
        snprintf(clock, sizeof(clock), "%02d:%02d:%02d", g.tm_hour, g.tm_min, g.tm_sec);

        if (_logStatus && (_statusFrame++ % 200) == 0)
        {
            OSG_NOTICE << "[Status] feeds " << okN << "/" << enN
                       << " preset=" << preset << std::endl;
        }

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 0.0f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.0f));   // 顶部居中锚定
        ImGui::SetNextWindowBgAlpha(0.55f);
        if (ImGui::Begin("##earth_statusbar", NULL,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize
                         | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                         | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs))
        {
            ImGui::Text(u8"UTC %s  ·  数据源 %d/%d  ·  预设 %s",
                        clock, okN, enN, preset.c_str());
        }
        ImGui::End();
    }
};

#endif
