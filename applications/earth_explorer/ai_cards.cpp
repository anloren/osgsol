// applications/earth_explorer/ai_cards.cpp
// 从 ai_ui.cpp 抽出(Task 8 PART A):卡片容器 AICardPanel 的存储/堆叠/绘制 + 4 种
// 图表手绘渲染器 + 数值/取值小工具。图表部分纯移动,逻辑与原 ai_ui.cpp 版本逐行一致;
// 照片卡(drawPhotoCard/pushPhoto)是 Task 8 新增。
#include "ai_cards.h"
#include "ui_card.h"
#include <ui/ImGuiComponents.h>
#include <osg/Notify>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

// 由 show_chart 工具的 execute 调用。execute 只在 AIChatCore::drainMainThread 里跑，
// 而 drain 又是从 FRAME handler（主线程）触发。但 viewer 是多线程
// (DrawThreadPerContext,见 EarthExplorer setThreadingModel 未启用 SingleThreaded),
// ImGui 内容(registerCards/drawBody)跑在 POST_DRAW 相机回调=draw 线程,与本函数所在
// 的主线程并发访问 _cards——push_back 触发 realloc 时 draw 线程持有的元素引用会悬垂
// (UAF),必须加锁(review 2026-07-08)。
void AICardPanel::pushChart(const picojson::value& spec)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
    AICard c; c.type = AICard::CHART; c.spec = spec; c.open = true;
    c.serial = _nextSerial++;
    _cards.push_back(c);
}

// 由 MediaManager::update() 在生图/生视频 Job 完成(DONE)时调用。viewer 多线程,_cards
// 由 draw 线程 registerCards 并发访问,必须加锁(见上方 pushChart 注释,review 2026-07-08)。
// v0.15-vision 收尾修复:同类型(照片或视频,靠 isVideo 区分,互不影响)结果卡任意时刻
// 只保留最新一张,不再无限堆叠——仿照 removeJob() 的"进度卡→结果卡"替换模式,这里
// 扩展成"旧结果卡→新结果卡"也替换。只关同 isVideo 的旧卡,生成新照片不应该带走
// 用户还想看的旧视频卡,反之亦然。整个"关旧卡+push新卡"在同一把锁内完成,避免
// 两次加锁之间被 draw 线程的 registerCards 看到中间状态。
void AICardPanel::pushPhoto(const std::string& pngPath, const std::string& title, bool isVideo)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
    for (size_t i = 0; i < _cards.size(); ++i)
        if (_cards[i].type == AICard::PHOTO && _cards[i].isVideo == isVideo && _cards[i].open)
        {
            _cards[i].open = false;
            OSG_NOTICE << "[AIChat] closed previous " << (isVideo ? "video" : "photo")
                       << " card serial=" << _cards[i].serial << std::endl;
        }

    AICard c; c.type = AICard::PHOTO; c.path = pngPath; c.title = title; c.isVideo = isVideo; c.open = true;
    c.serial = _nextSerial++;
    _cards.push_back(c);
}

// 由 MediaManager::startPhotoJob() 在建 Job 后立即调用。viewer 多线程,_cards 由 draw
// 线程 registerCards 并发访问,必须加锁(见上方 pushChart 注释,review 2026-07-08)。
void AICardPanel::pushJob(earthai::JobManager* jobs, int jobId, const std::string& title)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
    AICard c; c.type = AICard::JOB; c.jobs = jobs; c.jobId = jobId; c.title = title; c.open = true;
    c.serial = _nextSerial++;
    _cards.push_back(c);
}

// 由 MediaManager::update() 在 Job 结束(DONE/FAILED)时调用,把进度卡从堆叠里摘掉——
// DONE 那一刻调用方会紧接着 pushPhoto() 换上结果卡,FAILED 则直接消失(错误已走 OSG_WARN)。
// viewer 多线程,_cards 由 draw 线程 registerCards 并发访问,必须加锁(见上方 pushChart
// 注释,review 2026-07-08)。
void AICardPanel::removeJob(int jobId)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
    for (size_t i = 0; i < _cards.size(); ++i)
        if (_cards[i].type == AICard::JOB && _cards[i].jobId == jobId) { _cards[i].open = false; break; }
}

// v0.15-vision:堆叠定位/换列/表头绘制统一交给 earthui::CardStack(ui_card.h),
// 本函数只负责"把当前存活的卡片翻译成 Card 结构体登记进去"。已关闭的卡片
// 在遍历/登记之前先清掉(erase 打乱下标要放在这里做,不能等 CardStack 绘制完再做——
// 那时"哪些卡关闭了"取决于本帧用户点击 [x],必须等下一帧再清理)。
void AICardPanel::registerCards(earthui::CardStack& stack)
{
    // 全程持锁:viewer 多线程(DrawThreadPerContext),本函数跑在 POST_DRAW draw 线程,
    // 与主线程的 pushChart/pushPhoto/pushJob/removeJob 并发访问 _cards。erase 和下面的
    // for 循环都必须在锁内,否则主线程可能在两者之间 push_back 触发 realloc(review 2026-07-08)。
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);

    _cards.erase(std::remove_if(_cards.begin(), _cards.end(),
                                 [](const AICard& c) { return !c.open; }),
                 _cards.end());

    for (size_t i = 0; i < _cards.size(); ++i)
    {
        AICard& c = _cards[i];

        std::string title = u8"图表 Chart";
        if (c.type == AICard::CHART)
        {
            if (c.spec.is<picojson::object>() && c.spec.contains("title")
                && c.spec.get("title").is<std::string>())
                title = c.spec.get("title").get<std::string>();
        }
        else if (c.type == AICard::PHOTO)
            title = c.title.empty() ? u8"实景照片" : c.title;
        else if (c.type == AICard::JOB)
            title = c.title.empty() ? u8"生成中" : c.title;

        earthui::Card card;
        card.id = "ai_" + std::to_string(c.serial);   // serial 存活期间不变(同原 ID 拼接惯例)
        card.style.chipLabel = u8"AI";
        card.title = title;
        // 按值快照(而非按引用捕获 c):CardStack::draw() 真正调用这个 lambda 的时机
        // 在 registerCards() 返回之后,期间主线程随时可能 pushChart/pushPhoto/pushJob
        // 触发 _cards realloc,使 &c 悬垂——这是本任务要修的 UAF 根因(review 2026-07-08)。
        // 按值拷贝一份 AICard 快照,drawBody 执行时不再触碰 _cards。
        AICard snapshot = c;
        card.drawBody = [this, snapshot]() {
            // 用内容区宽度(已扣除 WindowPadding)而不是窗口全宽常量,否则条形图/折线图
            // 右缘的数值文字会被 WindowPadding 裁掉一部分。
            if (snapshot.type == AICard::CHART) drawChartCard(snapshot.spec, ImGui::GetContentRegionAvail().x);
            else if (snapshot.type == AICard::PHOTO) drawPhotoCard(snapshot);
            else if (snapshot.type == AICard::JOB) drawJobCard(snapshot);
            else ImGui::TextDisabled(u8"（暂未实现的卡片类型）");
        };
        // onClose 同理不能捕获 &c,按 serial 定位(closeBySerial 内部自己加锁)。
        int serial = c.serial;
        card.onClose = [this, serial]() { closeBySerial(serial); };
        stack.upsert(card);
    }
}

// 由卡片 [x] 的 onClose(draw 线程)调用:按 serial 定位并标记关闭(下一帧 registerCards 在锁内 erase)。
// 不能像旧代码那样捕获 &c——_cards 可能已被主线程 push 触发 realloc,引用会悬垂。
void AICardPanel::closeBySerial(int serial)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
    for (size_t i = 0; i < _cards.size(); ++i)
        if (_cards[i].serial == serial) { _cards[i].open = false; break; }
}

// ---- 数值取值小工具:picojson 的 JSON number 一律存成 double,int/double 字面量都能用 is<double>() 判出 ----
static double numOr(const picojson::value& v, double fallback)
{
    return v.is<double>() ? v.get<double>() : fallback;
}

// ---- 数值格式化小工具:整数样式的值用 %lld 显示,否则 %.1f ----
// 原先各处直接写 `v == (double)(long long)v` 判断"是不是整数",对 NaN/inf/
// 超出 long long 范围的巨大值(如 1e20)做 (long long) 转换是未定义行为(UB)。
// 这里先做有限性+范围检查,异常值统一走 %.3g 兜底,再进整数/小数分支。
static void formatNum(char* buf, size_t n, double v)
{
    if (!(fabs(v) < 9e15))   // 覆盖 NaN(比较恒假)、+-inf、超出安全整数范围的巨大值
    {
        snprintf(buf, n, "%.3g", v);
        return;
    }
    long long iv = (long long)v;
    if (v == (double)iv) snprintf(buf, n, "%lld", iv);
    else snprintf(buf, n, "%.1f", v);
}

// 从 spec 里取 labels[]/values[] 数组,分别转成 string/double 数组。
// 以 values 为准对齐:labels 不足时用 "" 补齐到 values.size(),labels 多则截断。
// 工具 schema 的 required 只有 type+values(见 ai_setup.cpp show_chart.parametersJson),
// labels 本就是可选参数(stat 图的 labels[0] 副标题尤其常被省略),不能按 min 长度双向
// 截断——否则合法调用如 {"type":"stat","values":[7]} 会被截成空数据,整卡显示"无数据"。
static void extractLabelsValues(const picojson::value& spec,
                                std::vector<std::string>& labels, std::vector<double>& values)
{
    if (spec.contains("labels") && spec.get("labels").is<picojson::array>())
    {
        const picojson::array& arr = spec.get("labels").get<picojson::array>();
        for (size_t i = 0; i < arr.size(); ++i)
            labels.push_back(arr[i].is<std::string>() ? arr[i].get<std::string>() : arr[i].to_str());
    }
    if (spec.contains("values") && spec.get("values").is<picojson::array>())
    {
        const picojson::array& arr = spec.get("values").get<picojson::array>();
        for (size_t i = 0; i < arr.size(); ++i)
            values.push_back(numOr(arr[i], 0.0));
    }
    labels.resize(values.size());   // 不足补 ""(resize 默认值),多则截断
}

// 六色调色板(hue 均分),donut 分片按序取色；用于区分不同切片而不需要每次算 HSV。
static ImU32 sliceColor(size_t idx)
{
    static const ImU32 palette[6] = {
        IM_COL32(90, 170, 255, 255),   // 蓝(accent)
        IM_COL32(255, 170, 60, 255),   // 橙
        IM_COL32(90, 220, 140, 255),   // 绿
        IM_COL32(230, 90, 140, 255),   // 粉
        IM_COL32(190, 130, 255, 255),  // 紫
        IM_COL32(240, 220, 80, 255),   // 黄
    };
    return palette[idx % 6];
}

static void drawBarChart(ImDrawList* dl, ImVec2 origin, float width,
                         const std::vector<std::string>& labels, const std::vector<double>& values)
{
    const ImU32 accent = IM_COL32(90, 170, 255, 255);
    const ImU32 track = IM_COL32(255, 255, 255, 25);
    double maxV = 0.0;
    for (size_t i = 0; i < values.size(); ++i) maxV = std::max(maxV, values[i]);

    const float rowH = 22.0f, rowGap = 6.0f;
    const float labelW = 56.0f, valueW = 40.0f;
    float barAreaW = width - labelW - valueW - 8.0f;
    if (barAreaW < 20.0f) barAreaW = 20.0f;

    float y = origin.y;
    for (size_t i = 0; i < values.size(); ++i)
    {
        // 标签(左)
        dl->AddText(ImVec2(origin.x, y + 3.0f), IM_COL32(220, 220, 220, 255), labels[i].c_str());

        // 条形背景 track + 前景 bar(0/负值钳到 0 长度,但数值文字仍照常显示)
        float barX = origin.x + labelW;
        dl->AddRectFilled(ImVec2(barX, y), ImVec2(barX + barAreaW, y + rowH), track, 4.0f);
        double v = values[i]; if (v < 0.0) v = 0.0;
        float frac = (maxV > 0.0) ? (float)(v / maxV) : 0.0f;
        float barW = barAreaW * frac;
        if (barW > 1.0f)
            dl->AddRectFilled(ImVec2(barX, y), ImVec2(barX + barW, y + rowH), accent, 4.0f);

        // 数值(右)
        char buf[32];
        formatNum(buf, sizeof(buf), values[i]);
        dl->AddText(ImVec2(barX + barAreaW + 8.0f, y + 3.0f), IM_COL32(230, 230, 230, 255), buf);

        y += rowH + rowGap;
    }
    ImGui::Dummy(ImVec2(width, y - origin.y));
}

// imgui.h(公开头)不带 IM_PI(定义在 imgui_internal.h，本文件不想拉内部头)，本地补一个。
static const float kTwoPi = 6.28318530718f;

static void drawDonutChart(ImDrawList* dl, ImVec2 origin, float width,
                           const std::vector<std::string>& labels, const std::vector<double>& values)
{
    double total = 0.0;
    for (size_t i = 0; i < values.size(); ++i) total += std::max(0.0, values[i]);

    const float radius = 58.0f, thickness = 18.0f;
    ImVec2 center(origin.x + width * 0.5f, origin.y + radius);

    if (total <= 0.0)
    {
        dl->AddCircle(center, radius, IM_COL32(255, 255, 255, 40), 64, thickness);
    }
    else
    {
        float startAngle = -kTwoPi * 0.25f;   // 12 点钟方向起(-90°)
        for (size_t i = 0; i < values.size(); ++i)
        {
            double v = std::max(0.0, values[i]);
            if (v <= 0.0) continue;
            float sweep = (float)(v / total) * kTwoPi;
            dl->PathArcTo(center, radius, startAngle, startAngle + sweep, 48);
            dl->PathStroke(sliceColor(i), 0, thickness);
            startAngle += sweep;
        }
    }

    // 中心大数字(总计)
    char totBuf[32];
    formatNum(totBuf, sizeof(totBuf), total);
    ImVec2 tSize = ImGui::CalcTextSize(totBuf);
    dl->AddText(ImVec2(center.x - tSize.x * 0.5f, center.y - tSize.y * 0.5f),
               IM_COL32(255, 255, 255, 255), totBuf);

    ImGui::Dummy(ImVec2(width, radius * 2.0f + 4.0f));

    // 图例:色点 + 标签 + 数值,逐行排列
    for (size_t i = 0; i < values.size(); ++i)
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(p.x + 6.0f, p.y + 8.0f), 5.0f, sliceColor(i));
        ImGui::Dummy(ImVec2(16.0f, 0.0f));
        ImGui::SameLine();
        char numBuf[32], buf[96];
        formatNum(numBuf, sizeof(numBuf), values[i]);
        snprintf(buf, sizeof(buf), "%s: %s", labels[i].c_str(), numBuf);
        ImGui::TextUnformatted(buf);
    }
}

static void drawLineChart(ImDrawList* dl, ImVec2 origin, float width,
                          const std::vector<std::string>& labels, const std::vector<double>& values)
{
    const float chartH = 90.0f;
    double vmin = values[0], vmax = values[0];
    for (size_t i = 1; i < values.size(); ++i) { vmin = std::min(vmin, values[i]); vmax = std::max(vmax, values[i]); }
    double range = vmax - vmin;
    if (range <= 0.0) range = 1.0;   // 全等值时避免除零,画一条水平线

    std::vector<ImVec2> pts(values.size());
    float stepX = (values.size() > 1) ? width / (float)(values.size() - 1) : 0.0f;
    for (size_t i = 0; i < values.size(); ++i)
    {
        float t = (float)((values[i] - vmin) / range);
        float x = origin.x + stepX * (float)i;
        float y = origin.y + chartH - t * chartH;
        pts[i] = ImVec2(x, y);
    }

    // 折线下方填充:逐段画梯形(而不是把整条折线所有顶点丢给 AddConvexPolyFilled)。
    // 折线顶边 + 底边两角拼成的多边形,只要数据不是单调的(如 420→455→430→500→480
    // 这种有起伏的锯齿数据)就是凹多边形——ImGui 的 convex fill 对凹多边形填色会出错,
    // 颜色可能涂到折线上方、或半透明区深浅不均。改成对每相邻两点 pts[i]/pts[i+1] 单独
    // 画一个梯形(两个顶点在折线上、两个顶点在底边上),每个梯形都必然是凸的,不会出错。
    if (pts.size() >= 2)
    {
        const ImU32 fillCol = IM_COL32(90, 170, 255, 40);
        float baseY = origin.y + chartH;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            dl->AddQuadFilled(pts[i], pts[i + 1], ImVec2(pts[i + 1].x, baseY), ImVec2(pts[i].x, baseY),
                              fillCol);
        }
    }
    if (pts.size() >= 2)
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(90, 170, 255, 255), 0, 2.0f);
    for (size_t i = 0; i < pts.size(); ++i)
        dl->AddCircleFilled(pts[i], 3.0f, IM_COL32(255, 255, 255, 220));

    // 轴范围标注(左上角=max,左下角=min)
    char maxBuf[32], minBuf[32];
    snprintf(maxBuf, sizeof(maxBuf), "%.1f", vmax);
    snprintf(minBuf, sizeof(minBuf), "%.1f", vmin);
    dl->AddText(ImVec2(origin.x, origin.y - 2.0f), IM_COL32(180, 180, 180, 255), maxBuf);
    dl->AddText(ImVec2(origin.x, origin.y + chartH - 12.0f), IM_COL32(180, 180, 180, 255), minBuf);

    ImGui::Dummy(ImVec2(width, chartH + 6.0f));

    // 首尾标签(数据点多时中间标签容易挤在一起,先只标首尾,够用)
    if (!labels.empty())
    {
        ImGui::TextDisabled("%s", labels.front().c_str());
        if (labels.size() > 1) { ImGui::SameLine(width - ImGui::CalcTextSize(labels.back().c_str()).x); ImGui::TextDisabled("%s", labels.back().c_str()); }
    }
}

static void drawStatChart(const std::vector<std::string>& labels, const std::vector<double>& values)
{
    double v = values.empty() ? 0.0 : values[0];
    char buf[32];
    formatNum(buf, sizeof(buf), v);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetWindowFontScale(2.2f);
    ImGui::TextUnformatted(buf);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    if (!labels.empty()) ImGui::TextDisabled("%s", labels[0].c_str());
}

void AICardPanel::drawChartCard(const picojson::value& spec, float width)
{
    if (!spec.is<picojson::object>()) { ImGui::TextDisabled(u8"无数据"); return; }

    std::string type = spec.contains("type") && spec.get("type").is<std::string>()
                        ? spec.get("type").get<std::string>() : "bar";

    std::vector<std::string> labels;
    std::vector<double> values;
    extractLabelsValues(spec, labels, values);

    if (values.empty())
    {
        // 居中的"无数据"提示
        const char* msg = u8"无数据";
        float avail = ImGui::GetContentRegionAvail().x;
        float tw = ImGui::CalcTextSize(msg).x;
        if (avail > tw) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) * 0.5f);
        ImGui::TextDisabled("%s", msg);
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();

    if (type == "donut") drawDonutChart(dl, origin, width, labels, values);
    else if (type == "line") drawLineChart(dl, origin, width, labels, values);
    else if (type == "stat") drawStatChart(labels, values);
    else drawBarChart(dl, origin, width, labels, values);   // 默认/未知类型按 bar 处理
}

// ---- 照片/视频卡(Task 8 照片 + Task 9 视频复用同一种卡片):文件名 + 「打开」按钮,
// 用系统默认查看器/播放器打开(macOS `open`,按文件扩展名自动派发给图片查看器或
// QuickTime/系统默认播放器,PNG/MP4 都不需要额外处理)。----
// 缩略图/视频预览帧故意跳过:ImGui 需要把图像解码上传成 GL 纹理再画 image()——这条纹理
// 管线在本文件(纯 ImDrawList 手绘,零额外依赖)里没有现成基础设施,留给后续任务按需补。
static std::string basename(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

void AICardPanel::drawPhotoCard(const AICard& c)
{
    ImGui::TextWrapped("%s", basename(c.path).c_str());
    if (c.path.empty())
    {
        ImGui::TextDisabled(u8"（无文件）");
        return;
    }
    if (ImGui::Button(u8"打开"))
    {
        // macOS `open`;路径用单引号包住防止空格/特殊字符断开命令(与 spec 约定一致)。
        std::string cmd = "open '" + c.path + "'";
        int rc = system(cmd.c_str());
        if (rc != 0)
            OSG_WARN << "[AIChat] open " << (c.isVideo ? "video" : "photo")
                     << " failed, rc=" << rc << " path=" << c.path << std::endl;
    }
}

// ---- 进行中任务卡(Task 8):转轮 + 进度条,实时从 JobManager 读(不缓存快照,避免过期)。
// DONE/FAILED 由 MediaManager::update() 主动 removeJob() 摘掉,这里画的始终是"进行中"态,
// 因此拿不到 job(理论上不会发生,防御一下)就显示占位文案而不是崩溃。
void AICardPanel::drawJobCard(const AICard& c)
{
    earthai::AIJob job;
    if (!c.jobs || !c.jobs->get(c.jobId, job))
    {
        ImGui::TextDisabled(u8"（任务信息不可用）");
        return;
    }
    static const char spin[4] = { '|', '/', '-', '\\' };
    int idx = (int)(ImGui::GetTime() * 8.0) % 4;
    ImGui::Text(u8"生成中 %c", spin[idx]);
    ImGui::ProgressBar(job.progress, ImVec2(-1.0f, 0.0f));
}
