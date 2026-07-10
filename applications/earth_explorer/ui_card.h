#ifndef EARTH_UI_CARD_H
#define EARTH_UI_CARD_H

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <osg/Vec2>
#include <osg/Vec4>
#include <ui/ImGuiComponents.h>
#include "marker_style.h"

// v0.15-vision Task 5:统一的右上角信息卡组件。取代 ai_cards.cpp / event_ticker.h /
// EarthControlUI.h 三处各自手搓的堆叠+表头绘制代码——以后任何模块要展示一张信息卡,
// 只需构造一个 Card、调用 CardStack::upsert(),不必重新实现定位/换列/表头。
//
// 使用约定(每帧):各生产者(ai_cards/event_ticker/EarthControlUI 的详情卡)在本帧
// "应该显示"时调用一次 upsert();不应该显示的卡片(如未选中任何要素)不调用即可,
// 不需要显式 remove()——CardStack 每帧的可见卡片列表由当帧的 upsert 调用决定。
// 帧末由持有 CardStack 实例的一方(EarthControlUI)调用一次 draw(),统一定位+
// ImGui::Begin/End 绘制所有本帧被 upsert 过的卡片,然后清空供下一帧使用。
// 卡片高度(AlwaysAutoResize 窗口)只有在 End() 之后才确定,draw() 内部用一个
// id->上一帧高度的缓存做溢出换列预判(同 ai_cards.cpp 原有手法),这个缓存跨帧持久。
namespace earthui
{
    struct CardStyle
    {
        osg::Vec4 accentColor = osg::Vec4(0.231f, 0.655f, 1.0f, 1.0f);   // 默认主题强调色 #3ba7ff
        std::string chipLabel;   // 表头胶囊文字,如 "AI"/"航班"/"要素"/"事件流"
        earthmark::MarkerShape shape = earthmark::MarkerShape::Circle;   // 表头胶囊 icon 形状(外观)
    };

    struct Card
    {
        std::string id;                       // 稳定 ID(不随卡片增删变化,用于 ImGui 窗口"###"后缀)
        CardStyle style;
        std::string title;                    // ImGui 窗口标题(原生标题栏,含 [x] 关闭按钮)
        std::string subtitle;                 // 表头胶囊右侧小字(通常是时间/来源附加信息),可空
        std::function<void()> drawBody;       // 正文内容,自由绘制(图表/图片/进度条/列表)
        std::function<void()> onClose;        // 点击原生标题栏 [x] 时调用;可空
        bool closable = true;
    };

    // 纯函数(不依赖 ImGui/OSG 之外的类型):给定每张卡的高度提示(0 = 未知,用 estimateHeight
    // 兜底)与布局参数,按登记顺序算出每张卡的锚点坐标(pivot(1,0),即右上角锚点对应的
    // 屏幕坐标)。右上角向下堆叠,叠到 screenH - bottomReserve 放不下时向左开新列、
    // 回到 topY 顶部继续堆叠。可单元测试,见 tests/feed_layer_tests.cpp。
    inline std::vector<osg::Vec2> computeCardLayout(
        const std::vector<float>& heightHints, float screenW, float screenH,
        float rightMargin, float cardWidth, float cardGap,
        float topY, float bottomReserve, float estimateHeight)
    {
        std::vector<osg::Vec2> out; out.reserve(heightHints.size());
        float curX = screenW - rightMargin;
        float curY = topY;
        for (size_t i = 0; i < heightHints.size(); ++i)
        {
            float predictH = (heightHints[i] > 0.0f) ? heightHints[i] : estimateHeight;
            if (curY + predictH > screenH - bottomReserve && curY > topY)
            {
                curX -= cardWidth + cardGap;
                curY = topY;
            }
            out.push_back(osg::Vec2(curX, curY));
            curY += predictH + cardGap;
        }
        return out;
    }

    // 右上角小圆角"胶囊"标签(数据来源徽标),手绘(ImGui 无原生胶囊控件)。
    inline void drawChip(const char* label, const osg::Vec4& color,
                         earthmark::MarkerShape shape = earthmark::MarkerShape::Circle)
    {
        if (!label || !label[0]) return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float ih = ImGui::GetTextLineHeight();
        ImU32 icol = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x(), color.y(), color.z(), 1.0f));
        earthmark::drawMarkerIcon(dl, shape, p.x + ih*0.5f, p.y + ih*0.5f, ih, icol);
        float iconAdvance = ih + 4.0f;
        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 padding(7.0f, 1.0f);
        ImVec2 chipSize(iconAdvance + textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
        ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x(), color.y(), color.z(), 0.18f));
        ImU32 fg = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x(), color.y(), color.z(), 1.0f));
        dl->AddRectFilled(p, ImVec2(p.x + chipSize.x, p.y + chipSize.y), bg, chipSize.y * 0.5f);
        dl->AddText(ImVec2(p.x + iconAdvance + padding.x, p.y + padding.y), fg, label);
        ImGui::Dummy(chipSize);
    }

    class CardStack
    {
    public:
        // 本帧登记一张要显示的卡片(可多次调用,每次一张)。
        void upsert(const Card& card) { _frameCards.push_back(card); }

        // 帧末调用一次:定位 + 绘制本帧全部登记过的卡片,然后清空供下一帧使用。
        void draw()
        {
            static const float kRightMargin = 20.0f, kCardWidth = 340.0f, kCardGap = 12.0f;
            static const float kTopY = 20.0f, kBottomReserve = 160.0f, kEstimateHeight = 200.0f;

            ImGuiIO& io = ImGui::GetIO();
            std::vector<float> heightHints; heightHints.reserve(_frameCards.size());
            for (size_t i = 0; i < _frameCards.size(); ++i)
            {
                std::map<std::string, float>::iterator it = _lastHeight.find(_frameCards[i].id);
                heightHints.push_back(it != _lastHeight.end() ? it->second : 0.0f);
            }
            std::vector<osg::Vec2> pos = computeCardLayout(heightHints, io.DisplaySize.x, io.DisplaySize.y,
                kRightMargin, kCardWidth, kCardGap, kTopY, kBottomReserve, kEstimateHeight);

            for (size_t i = 0; i < _frameCards.size(); ++i)
            {
                Card& c = _frameCards[i];
                ImGui::SetNextWindowPos(ImVec2(pos[i].x(), pos[i].y()), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(kCardWidth, 0.0f), ImGuiCond_Always);

                bool open = true;
                // ImGui 窗口身份只看 "###" 后的部分——用卡片稳定 id 拼后缀,不用下标
                // (ai_cards.cpp 踩过的坑:下标随卡片增删变化,会被认成不同窗口导致闪烁)。
                std::string winId = c.title + "###card_" + c.id;
                if (ImGui::Begin(winId.c_str(), c.closable ? &open : NULL,
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
                {
                    if (!c.style.chipLabel.empty())
                    {
                        drawChip(c.style.chipLabel.c_str(), c.style.accentColor, c.style.shape);
                        if (!c.subtitle.empty())
                        {
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s", c.subtitle.c_str());
                        }
                        ImGui::Separator();
                    }
                    if (c.drawBody) c.drawBody();
                    // 必须在 End() 之前读:GetWindowSize() 取的是"当前窗口"的尺寸
                    // (ai_cards.cpp 同款坑,见其注释)。
                    _lastHeight[c.id] = ImGui::GetWindowSize().y;
                }
                ImGui::End();
                if (c.closable && !open && c.onClose) c.onClose();
            }
            _frameCards.clear();
        }

    private:
        std::vector<Card> _frameCards;             // 本帧登记的卡片,draw() 末尾清空
        std::map<std::string, float> _lastHeight;   // 跨帧持久:id -> 上一帧高度(溢出换列预判用)
    };
}

#endif
