// FeedLayer:从 quake_data.cpp 提炼的通用"世界数据源"框架实现。
// 抓取线程/主线程同步/点精灵着色器/拾取/AI 工具注册,全部只写一份,
// 每个数据源只需提供一份 FeedSpec(url/parse/summaryJson/...)。
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Point>
#include <osg/NodeCallback>
#include <osg/Timer>
#include <osgDB/FileUtils>
#include <osgGA/GUIEventHandler>
#include <ui/ImGuiComponents.h>   // ImGui:: 命名空间(GetCurrentContext/GetIO)在这里,不在 ui/ImGui.h
#include <OpenThreads/Thread>
#include <OpenThreads/Mutex>
#include <modeling/Math.h>
#include <pipeline/Pipeline.h>
#include <pipeline/Utilities.h>
#include <readerwriter/EarthManipulator.h>
#include <VerseCommon.h>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <map>
#include <utility>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdio>
#include "3rdparty/libhv/all/client/requests.h"
#include "feed_layer.h"
#include "geo_primitives.h"
#include "earth_config.h"   // http.retries(设置面板可调的连接层重试次数,任务A同期加入)

namespace earthfeed
{
namespace
{
    // P3 修复:抬升量改由每源 FeedSpec::liftMeters 决定(默认仍是 3000,与本常量数值
    // 一致)——本常量不再被计算路径直接使用,只作为默认值的文档锚点保留,避免读者
    // 找不到"3000 从哪来"。三处原用它的计算(fetchOnce/buildClusterLevels/
    // correctRecordsToTerrain)均已改用 _spec.liftMeters。
    const double kFeedLiftMeters = 3000.0;   // 标记抬离地表默认值,防 z-fight、保可见(同 quake)

    // 点精灵着色器:与 quake_data.cpp 完全同构,唯一区别是"颜色/大小已由 FeedPoint 决定"
    // (quake 版本从震级/深度公式现算,这里直接是逐点属性)——顶点/片元代码不需要再改。
    const char* feedVertCode = {
        "VERSE_VS_OUT vec4 pointColor;\n"
        "VERSE_VS_OUT float vShapeId;\n"
        "void main() {\n"
        "    pointColor = osg_Color;\n"
        "    gl_PointSize = osg_MultiTexCoord0.x;\n"
        "    vShapeId = osg_MultiTexCoord0.y;\n"
        // 前半球剔除(取代深度测试):点顶点是 ECEF 世界坐标(地心在原点,Model=I 挂 sceneCamera),\n"
        // 变到相机空间后地心 Cv=MV*原点、相机在原点。世界系可见判据 (eye·P>|P|²) 等价于\n"
        // 相机空间的 dot(Pv-Cv, Pv)<0(向量点积在刚体变换下不变,与 pickAt 同款数学)。\n"
        // 背面点直接移出裁剪空间。这样点可以关掉深度测试(见 buildScene),彻底摆脱远视角\n"
        // 深度精度不足导致的 z-fight——原症状是拉远转动地球时标记点不断闪烁。\n"
        "    vec4 Pv = VERSE_MATRIX_MV * osg_Vertex;\n"
        "    vec3 Cv = (VERSE_MATRIX_MV * vec4(0.0, 0.0, 0.0, 1.0)).xyz;\n"
        "    if (dot(Pv.xyz - Cv, Pv.xyz) >= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }\n"
        "    gl_Position = VERSE_MATRIX_MVP * osg_Vertex;\n"
        "}\n"
    };
    // 片元入口:实际形状绘制逻辑在 earthmark::markerShapeGLSL() 共享库里(buildScene()
    // 拼接在本字符串之前,提供 markerCoverage),这里只调用它——所有 9 个 feed 源
    // (+战略)按 vShapeId(=texcoord0.y,构造时从 earthmark::visualForLayer(spec.id)
    // 取)自动获得对应形状,不用逐源改 shader。
    const char* feedFragMain = {
        "VERSE_FS_IN vec4 pointColor;\n"
        "VERSE_FS_IN float vShapeId;\n"
        "#ifdef VERSE_GLES3\n"
        "layout(location = 0) VERSE_FS_OUT vec4 fragColor;\n"
        "layout(location = 1) VERSE_FS_OUT vec4 fragOrigin;\n"
        "#endif\n"
        "void main() {\n"
        "    float cov = markerCoverage(int(vShapeId + 0.5), gl_PointCoord, 0.0);\n"
        "    if (cov <= 0.0) discard;\n"
        "    vec3 rgb = mix(pointColor.rgb * 0.7, pointColor.rgb, cov);\n"
        "#ifdef VERSE_GLES3\n"
        "    fragColor = vec4(rgb, 1.0); fragOrigin = vec4(1.0);\n"
        "#else\n"
        "    gl_FragData[0] = vec4(rgb, 1.0); gl_FragData[1] = vec4(1.0);\n"
        "#endif\n"
        "}\n"
    };

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

    const float kDragThreshPx2     = 25.0f;   // 5px 拖动阈值的平方(同 quake)
    const float kPickExtraRadiusPx = 10.0f;   // 拾取容差:点半径外再加的像素裕量

    // 跨全部 feed 的选中态(见 feed_layer.h 注释):file-scope,仅主线程读写。
    // 放在 FeedLayerImpl 之前定义,供其 pickAt() 调用。
    OpenThreads::Mutex& globalSelMutex() { static OpenThreads::Mutex m; return m; }
    FeedSelection& globalSelStorage() { static FeedSelection s; return s; }
    void setGlobalSelection(const FeedSelection& s)
    { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(globalSelMutex()); globalSelStorage() = s; }

    // 单条要素的可渲染快照:预算好的 ECEF 坐标 + FeedPoint 的其余展示字段。
    struct FeedRecord
    {
        FeedPoint pt;
        osg::Vec3d ecef;
    };

    // 一次抓取的完整产出:点 + 线/弧几何(parseGeometry 未设时 geometry 恒为空,
    // 纯点源行为与扩展前完全一致)。抓取线程整体产出、主线程整体消费。
    struct FeedSnapshot
    {
        std::vector<FeedRecord> records;
        FeedGeometry geometry;
        // P3 聚合 LOD:与 spec.cluster.levels 一一对应;cellDeg<=0 的级别恒为空
        // (显示时直接用 records,避免整份原始点重复存两遍)。抓取线程填、主线程消费。
        std::vector<std::vector<FeedRecord> > levels;
    };

    class FeedLayerImpl;

    // T8:全部 FeedLayerImpl 的全局注册表(事件流卡/状态带只读遍历用)。registerFeedLayer
    // 时登记、~FeedLayerImpl 自摘(单测里层节点是块作用域 ref_ptr,先于进程结束析构,
    // 不摘会留悬垂指针)。增删与遍历都在主线程,免锁。
    std::vector<FeedLayerImpl*>& globalImpls() { static std::vector<FeedLayerImpl*> v; return v; }

    class SyncCallback : public osg::NodeCallback
    {
    public:
        SyncCallback(FeedLayerImpl* o) : _owner(o) {}
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);
    protected:
        FeedLayerImpl* _owner;
    };

    class FetchThread : public OpenThreads::Thread
    {
    public:
        FetchThread(FeedLayerImpl* o) : _owner(o), _done(false) {}
        virtual int cancel() { _done = true; return OpenThreads::Thread::cancel(); }
        virtual void run();
    protected:
        FeedLayerImpl* _owner;
        // T1 复审carry-forward:抓取线程读写、主线程(析构)也写 → 用 atomic 而非普通 bool,
        // 避免跨线程无同步读写的数据竞争(UB)。
        std::atomic<bool> _done;
    };

    class FeedLayerImpl : public osg::Referenced
    {
    public:
        // T9(Yunnan 高程 bug 修复):viewer 由构造方(registerFeedLayer)传入,仅用于
        // syncIfDirty() 里主线程地形高度查询(见该函数注释)。裸指针而非 osg::ref_ptr/
        // observer_ptr——同 earth_main.cpp::_viewer / EarthControlUI.h::_viewer /
        // ai_media.h::_viewer 的既有约定:viewer 是 main() 里的栈上对象
        // (earth_main.cpp:615 `osgViewer::Viewer viewer;`),整个场景图(含本层节点)都挂
        // 在它下面、生命周期严格晚于它——FeedLayerImpl 不可能outlive它,不存在悬垂风险,
        // 无需 observer_ptr 的额外判空开销。默认 nullptr(而非改成必填):
        // tests/feed_layer_tests.cpp 直接 #include 本 .cpp、拿匿名命名空间里的 FeedLayerImpl
        // 单测(4 处 `new FeedLayerImpl(spec)`,无 viewer),按任务范围不改测试文件;
        // _viewer==nullptr 时 syncIfDirty() 里 `_viewer ? dynamic_cast<...> : nullptr`
        // 天然短路成 nullptr,地形矫正整段跳过,等价于该功能关闭——单测原有断言零受影响。
        FeedLayerImpl(const FeedSpec& spec, osgViewer::Viewer* viewer = nullptr)
            : _spec(spec), _shapeId((int)earthmark::visualForLayer(spec.id).shape), _root(nullptr),
              _viewer(viewer), _enabled(false), _lastFetchOk(false), _fetchState(0),
              _dirty(false), _thread(nullptr) {}

        void setEnabled(bool on)
        { _enabled = on; if (_root) _root->setNodeMask(on ? ~0u : 0u); if (!on) clearSelectionIfOurs(); }
        bool isEnabled() const { return _enabled; }

        void startFetch() { if (!_thread) { _thread = new FetchThread(this); _thread->startThread(); } }
        bool hasFetchThread() const { return _thread != nullptr; }

        // 静态源(T7 预留):url 为空且 fixtureEnv 对应环境变量已设置 → 注册时一次性
        // 读入 fixture,不起轮询线程(refreshSeconds 被忽略)。url 为空但 fixture 未设时
        // 不算静态源(维持旧行为:轮询线程对空 url 每轮报 fetch failed)。
        bool isStaticSource() const
        {
            if (!_spec.url.empty()) return false;
            if (!_spec.staticFile.empty()) return true;   // T7:打包内默认数据文件
            if (_spec.fixtureEnv.empty()) return false;
            const char* f = getenv(_spec.fixtureEnv.c_str());
            return f && *f;
        }

        // 取一次数据:fixtureEnv 优先(离线确定性验证),否则联网 GET spec.url。
        FeedSnapshot fetchOnce()
        {
            std::string body; bool ok = false; std::string errText;   // errText 只在失败时被填
            const char* fixtureFile = _spec.fixtureEnv.empty() ? nullptr : getenv(_spec.fixtureEnv.c_str());
            std::string localFile = (fixtureFile && *fixtureFile) ? std::string(fixtureFile) : std::string();
            // T7:静态源默认数据文件(url 空且 fixtureEnv 未设时才用;fixtureEnv 仍优先)
            if (localFile.empty() && _spec.url.empty() && !_spec.staticFile.empty()) localFile = _spec.staticFile;
            if (!localFile.empty())
            {
                std::ifstream in(localFile.c_str());
                if (!in)
                {
                    std::cout << "[Feed] " << _spec.id << " fixture open failed: " << localFile << "\n";
                    errText = u8"本地数据文件打开失败: " + localFile;
                }
                else { std::stringstream ss; ss << in.rdbuf(); body = ss.str(); ok = true; }
            }
            else
            {
                requests::Request req(new HttpRequest);
                req->method = HTTP_GET; req->url = _spec.url; req->timeout = _spec.timeoutSeconds;
                // 连接层重试(本任务):只在完全拿不到响应(resp 为空——超时/连不上/DNS 失败/
                // TLS 握手闪断等)时重试;一旦拿到任何响应(哪怕是 4xx/5xx),说明连接已经
                // 建立、服务端已经应答,重试没有意义,直接按原逻辑处理该状态码。次数读
                // earthcfg::getInt("http.retries")(设置面板可调,默认 3);线性退避
                // (attempt+1)*500ms,发生在抓取线程(FetchThread::run),不卡主线程。
                int retries = earthcfg::getInt("http.retries");
                if (retries < 0) retries = 0;
                requests::Response resp;
                for (int attempt = 0; attempt <= retries; ++attempt)
                {
                    resp = requests::request(req);
                    if (resp) break;   // 拿到响应(含错误状态码)即停止重试
                    if (attempt < retries)
                    {
                        std::cout << "[Feed] " << _spec.id << " connection-level failure, retry "
                                    << (attempt + 1) << "/" << retries << "\n";
                        OpenThreads::Thread::microSleep((attempt + 1) * 500 * 1000);
                    }
                }
                if (resp && resp->status_code == 200) { body = resp->body; ok = true; }
                else
                {
                    std::cout << "[Feed] " << _spec.id << " fetch failed, status="
                                << (resp ? (int)resp->status_code : -1) << "\n";
                    // 任务A:两种失败要说不同的话——有响应但非 200(如上游 404 死链)给出
                    // 具体状态码;完全没拿到响应(重试耗尽仍无响应)不能瞎编状态码。
                    errText = resp ? (u8"HTTP " + std::to_string((int)resp->status_code))
                                   : std::string(u8"网络连接失败(无响应)");
                }
            }
            _lastFetchOk = ok;   // T8 状态带健康统计(抓取线程写、主线程读 → atomic)
            // 任务A:三态抓取状态——ok→1,失败→2 且记下文案供 UI 展示(见 _fetchState 注释)。
            _fetchState = ok ? 1 : 2;
            if (!ok)
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_errMutex);
                _lastErrorText = errText.empty() ? std::string(u8"抓取失败(原因未知)") : errText;
            }
            FeedSnapshot out;
            if (!ok)
            {
                // 终审 C-1:失败快照的 levels 也必须与 cluster.levels 对齐,否则
                // syncIfDirty 聚合分支越界——buildClusterLevels 在 records 为空时
                // 仍会按 spec.cluster.levels.size() 逐级 push_back 一个空 vector,
                // 保证 out.levels.size() 恒等于 spec.cluster.levels.size()。
                buildClusterLevels(out);
                return out;
            }
            std::vector<FeedPoint> pts = _spec.parse ? _spec.parse(body) : std::vector<FeedPoint>();
            out.records.reserve(pts.size());
            for (size_t i = 0; i < pts.size(); ++i)
            {
                FeedRecord r; r.pt = pts[i];
                r.ecef = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
                    osg::DegreesToRadians(pts[i].lat), osg::DegreesToRadians(pts[i].lon), _spec.liftMeters));
                out.records.push_back(r);
            }
            // 线/弧几何(可选):与 parse 消费同一份 body;未设 = 纯点源,恒为空。
            if (_spec.parseGeometry) out.geometry = _spec.parseGeometry(body);
            std::cout << "[Feed] " << _spec.id << " loaded N=" << out.records.size();
            if (isStaticSource()) std::cout << " (static)";   // 静态源标记(T7 验证抓手)
            if (_spec.parseGeometry)
                std::cout << " arcs=" << out.geometry.arcs.size()
                          << " lines=" << out.geometry.lines.size();
            std::cout << "\n";
            buildClusterLevels(out);
            return out;
        }

        // 快照移交全程 move(评审修复):按值入参吃掉调用侧的临时量(fetchOnce() 返回值
        // 直接 move 进来),锁内 move 赋值不再深拷贝;syncIfDirty 锁内 swap 取走 _pending
        // (顺带释放其内存),再 move 进 _records(T8 起也持锁,见 syncIfDirty 注释)——
        // 大快照(千条记录)不再复制 3 次。
        void postSnapshot(FeedSnapshot snap)
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); _pending = std::move(snap); _dirty = true; }

        void syncIfDirty()
        {
            FeedSnapshot snap;
            { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
              if (!_dirty) return; std::swap(snap, _pending); _dirty = false; }
            // T8 评审修复:_records 的写入持 _mutex——viewer 非 SingleThreaded
            // (DrawThreadPerContext),事件流卡在 finalCamera POST_DRAW(draw 线程)
            // 经 snapshotRecords() 读记录,帧 N 的 draw 与帧 N+1 的 update 会重叠,
            // 无锁 move 赋值 = use-after-free。锁只包这一句,场景图重建仍在锁外。
            { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
              _records = std::move(snap.records);
              _levelRecords = std::move(snap.levels); }
            correctRecordsToTerrain();
            if (_spec.cluster.empty())
            {
                if (_geode.valid()) _root->removeChild(_geode.get());
                _geode = buildGeode();
                _root->addChild(_geode.get());
            }
            else
            {
                for (size_t i = 0; i < _levelGeodes.size(); ++i)
                    if (_levelGeodes[i].valid()) _root->removeChild(_levelGeodes[i].get());
                _levelGeodes.clear();
                // 终审 C-1 防线二:畸形快照(如失败快照曾经的旧行为)可能让 _levelRecords
                // 比 spec.cluster.levels 短——越界下标是 UB/崩溃。用一个空的静态兜底
                // vector 代替越界访问,保证本循环任何情况下都不越界(即使根因修复失效)。
                static const std::vector<FeedRecord> kEmptyLevel;
                for (size_t li = 0; li < _spec.cluster.levels.size(); ++li)
                {
                    const std::vector<FeedRecord>& recs =
                        (_spec.cluster.levels[li].cellDeg <= 0.0) ? _records
                        : (li < _levelRecords.size() ? _levelRecords[li] : kEmptyLevel);
                    _levelGeodes.push_back(buildGeodeFrom(recs));
                    _root->addChild(_levelGeodes.back().get());
                }
                int cur = _activeLevel; _activeLevel = -1;           // 强制重刷 nodeMask
                updateActiveLevel(cur >= 0 && cur < (int)_spec.cluster.levels.size()
                                  ? _spec.cluster.levels[cur].maxCameraAltKm * 0.5 : 1.0e9);
            }
            // 线/弧几何子图:每次刷新整组替换(不做增量 diff——一次快照就是一个完整世界态)。
            // 拾取仍只对点(pickAt 只遍历 _records):线/弧 v1 不可拾取,消费方(如 UNHCR)
            // 需要详情时应同时产出代表点(plan 既定取舍)。
            if (_geomGroup.valid()) { _root->removeChild(_geomGroup.get()); _geomGroup = nullptr; }
            if (!snap.geometry.arcs.empty() || !snap.geometry.lines.empty())
            {
                // 6a:线/弧 StateSet 实例内复用(_arcGeomSS/_lineGeomSS 懒创建于首次几何
                // sync、主线程)——重建几何只换顶点数据,不再每轮触发 1+L 次着色器编译。
                // 不跨 feed 实例共享、更不全局共享(4ee4c74d 雷区,见 feed_layer.h 注释)。
                _geomGroup = buildFeedGeometryGroup(snap.geometry, 65, _arcGeomSS, _lineGeomSS);
                _root->addChild(_geomGroup.get());
            }
        }

        // T9(Yunnan 高程 bug 修复,从 syncIfDirty() 抽出——质量审查建议,原逻辑不变):
        // spec.liftMeters 是"抬离地表"的设计意图,但 fetchOnce()(抓取线程)算的 ecef
        // 用的是 convertLLAtoECEF 的纯 WGS84 椭球高——modeling/Math.cpp:convertLLAtoECEF
        // 无 geoid/MSL 修正,height 直接加到基于纬度的曲率半径上。低海拔地区(椭球高≈0)
        // "离椭球 liftMeters"≈"离地表 liftMeters",看着没问题;但云南香格里拉一带(青藏
        // 高原东缘)地表椭球高常年 2000-4000m+,默认 liftMeters=3000 的标记会落在地表以下
        // (嵌进山体)——全球远景靠深度精度不足"露出来",一旦相机拉近到深度能正确分辨,
        // 标记被地形完全遮挡、凭空消失,正是复现报告的症状。P3 起该问题的第二道防线是
        // FeedSpec::liftMeters 可覆盖(见 feed_layer.h):高原易触发的源(如 FIRMS)可把
        // 默认值调到超过全球最高地表椭球高,使地形矫正变成"锦上添花"而非"生死线"——
        // 本函数(地形矫正)仍是精度改进路径,不再是唯一可见性保障。
        //
        // 修法:不改抓取线程的计算(那份 fixed-ellipsoid-height 结果保留作"地形查询
        // 失败"时的安全兜底,零回归风险),而是在这里(主线程,syncIfDirty() 调用,
        // _records 刚变成最新那一刻、buildGeode() 读它之前)对每条记录追加一次地形
        // 高度查询,查得到就用"真地表高度 + spec.liftMeters"覆盖 ecef。
        //
        // 为什么必须在主线程而不是抓取线程:EarthManipulator::terrainAltitudeAt
        // 内部对 _viewer->getCamera() 做 osgUtil::IntersectionVisitor 遍历,是读场景图
        // 操作,只能在主/update线程调用——这份文件已有的线程约定(见 syncIfDirty() 内
        // _records 加锁那段注释)反复强调过"worker 不可直接碰主线程拥有的东西"这类教训,
        // 地形求交同理,绝不能挪到 FetchThread::run() 里。
        //
        // dynamic_cast 而非 static_cast:manipulator 类型不对(极端/测试配置下没装
        // EarthManipulator)或 viewer 还没配好时,getCameraManipulator() 可能返回别的
        // 类型或本身逻辑上不可用,cast 结果要能表达"没有"并被判空跳过,不能崩。
        // 地形查询失败(terrainAltitudeAt 返回 false,如该经纬度瓦片尚未加载)时原样
        // 保留 fetchOnce() 算好的 fallback ecef——绝不允许比今天更差。
        void correctRecordsToTerrain()
        {
            osgVerse::EarthManipulator* mani = _viewer ?
                dynamic_cast<osgVerse::EarthManipulator*>(_viewer->getCameraManipulator()) : nullptr;
            if (!mani) return;

            // 性能量级(见 feed_layer.cpp 修复记录 / 报告):最重的源(GPSJam,量产
            // 数据可到 ~2900 点)每次新快照跑一次这个循环——不是每帧路径,新快照到达
            // 才触发(各源刷新间隔从秒级到天级不等,2900 点规模恰是刷新最慢的 GPSJam,
            // 天级一次),故这里的延迟不按每帧 16ms 预算衡量。实测(2026-07-05,离屏
            // 合成 N 点、真实 terrainAltitudeAt 求交,全球默认视角与云南近地视角结果一致,
            // 详见提交报告):N=50→0.33ms、N=300→2.0ms、N=1000→6.5ms、N=2900→~18ms,
            // 近线性(~6.3μs/点)。N=2900 那一帧会比通常稍长,但仍是毫秒级的一次性
            // stall,不是秒级卡顿,且只发生在 GPSJam 天级刷新的那一刻——按最简单的
            // 同步整循环实现,未做分帧摊销;如果未来出现更大规模数据源导致这个数字
            // 明显恶化,再考虑分帧摊销不迟。
            static const bool s_timing = (getenv("EARTH_FEED_TERRAIN_TIMING") != nullptr);
            // T9 验证钩子(修复真机高程 bug 用,非长期功能):EARTH_FEED_TERRAIN_VERIFY=1
            // 时,对每条记录打印"查到的真地表高度 + 相对它的最终抬升量",用于拿真实
            // 运行数字证明"抬升量≈spec.liftMeters(离真地表,而非离椭球)"——修复前
            // 用同样办法会看到抬升量随地形起伏乱跳(因为当时是相对椭球算的常数)。
            static const bool s_verify = (getenv("EARTH_FEED_TERRAIN_VERIFY") != nullptr);
            osg::Timer_t t0 = s_timing ? osg::Timer::instance()->tick() : 0;
            for (size_t i = 0; i < _records.size(); ++i)
            {
                double terrainAlt = 0.0;
                bool hit = mani->terrainAltitudeAt(
                    osg::DegreesToRadians(_records[i].pt.lat),
                    osg::DegreesToRadians(_records[i].pt.lon), terrainAlt);
                if (hit)
                {
                    _records[i].ecef = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
                        osg::DegreesToRadians(_records[i].pt.lat),
                        osg::DegreesToRadians(_records[i].pt.lon), terrainAlt + _spec.liftMeters));
                    if (s_verify)
                    {
                        // 关键数字:oldMarkerHeightAboveTerrain(修复前,标记恒定在椭球高
                        // _spec.liftMeters 处、相对"真地表"的净空 = _spec.liftMeters - terrainAlt)
                        // 若为负,说明修复前标记确实嵌进地形以下——这正是 bug。修复后
                        // newMarkerHeightAboveTerrain 恒为 _spec.liftMeters(按真地表重算),
                        // 与 terrainAlt 无关,证明"离地表 liftMeters"而非"离椭球 liftMeters"。
                        double oldHeightAboveTerrain = _spec.liftMeters - terrainAlt;
                        double newHeightAboveTerrain = _spec.liftMeters;   // 恒等于常数,按定义
                        OSG_NOTICE << "[FeedTerrainVerify] " << _spec.id << " #" << i
                                   << " lat=" << _records[i].pt.lat << " lon=" << _records[i].pt.lon
                                   << " terrainAlt=" << terrainAlt << "m"
                                   << " oldMarkerHeightAboveTerrain(BROKEN,ellipsoid-relative)="
                                   << oldHeightAboveTerrain << "m"
                                   << " newMarkerHeightAboveTerrain(FIXED,terrain-relative)="
                                   << newHeightAboveTerrain << "m" << std::endl;
                    }
                }
                // else: 保留 fetchOnce() 已算好的 fixed-ellipsoid-height 兜底 ecef 不变。
            }
            if (s_timing)
            {
                double ms = osg::Timer::instance()->delta_m(t0, osg::Timer::instance()->tick());
                std::cout << "[Feed] " << _spec.id << " terrain-correct N=" << _records.size()
                          << " took " << ms << " ms\n";
            }
        }

        osg::Geode* buildGeode() { return buildGeodeFrom(_records); }

        osg::Geode* buildGeodeFrom(const std::vector<FeedRecord>& recs)
        {
            osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
            osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
            osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
            osg::ref_ptr<osg::Vec2Array> sizes = new osg::Vec2Array;
            for (size_t i = 0; i < recs.size(); ++i)
            {
                verts->push_back(recs[i].ecef);
                colors->push_back(recs[i].pt.color);
                sizes->push_back(osg::Vec2(recs[i].pt.sizePx, (float)_shapeId));
            }
            geom->setVertexArray(verts.get());
            geom->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
            geom->setTexCoordArray(0, sizes.get());
            geom->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, (GLsizei)verts->size()));
            geom->setUseDisplayList(false); geom->setUseVertexBufferObjects(true);
            geom->setCullingActive(false);

            osg::ref_ptr<osg::Geode> geode = new osg::Geode;
            geode->addDrawable(geom.get());
            geode->setStateSet(_ss.get());
            return geode.release();
        }

        // 屏幕拾取(同 quake_data.cpp 的 pickAt):最近点、前半球剔除、像素容差。
        // P3:cluster 非空时只对"当前显示级别"的记录拾取(nodeMask 隐藏的级别不可点)。
        void pickAt(osg::Camera* cam, float mx, float my)
        {
            // 终审 C-1 防线二:_activeLevel 可能是上一份正常快照留下的旧值,而
            // 本份(畸形)快照的 _levelRecords 更短——两处下标(cluster.levels 与
            // _levelRecords)都必须先做范围检查,任何一处越界都直接落回 _records。
            bool activeLevelValid = _activeLevel >= 0
                && _activeLevel < (int)_spec.cluster.levels.size()
                && _activeLevel < (int)_levelRecords.size();
            const std::vector<FeedRecord>& recs =
                (_spec.cluster.empty() || !activeLevelValid
                 || _spec.cluster.levels[_activeLevel].cellDeg <= 0.0)
                ? _records : _levelRecords[_activeLevel];
            if (!_enabled || recs.empty() || !cam->getViewport()) return;
            osg::Vec3d eye, center, up; cam->getViewMatrixAsLookAt(eye, center, up);
            osg::Matrixd VPW = cam->getViewMatrix() * cam->getProjectionMatrix()
                             * cam->getViewport()->computeWindowMatrix();
            double bestD2 = 1e18; int best = -1;
            for (size_t i = 0; i < recs.size(); ++i)
            {
                const osg::Vec3d& P = recs[i].ecef;
                if ((eye * P) <= (P * P)) continue;   // 前半球剔除(见 quake_data.cpp 同名注释)
                osg::Vec3d win = P * VPW;
                double d2 = (win.x() - mx) * (win.x() - mx) + (win.y() - my) * (win.y() - my);
                float tol = recs[i].pt.sizePx * 0.5f + kPickExtraRadiusPx;
                if (d2 < (double)(tol * tol) && d2 < bestD2) { bestD2 = d2; best = (int)i; }
            }
            if (best >= 0)
            {
                // T1 复审carry-forward:选中态只有全局 FeedSelection 一份(展示端只读它),
                // 不再维护本 impl 私有的 _selected/_selMutex 副本——两份状态没有第二个读者,
                // 纯粹的死代码,删掉。
                FeedSelection s;
                s.valid = true; s.title = recs[best].pt.title; s.detail = recs[best].pt.detail;
                s.url = recs[best].pt.url;
                s.sourceId = _spec.id;   // 必修B:记来源,关本层时按它清选中
                setGlobalSelection(s);
            }
        }

        // 必修B:关闭本层时只清"来源==本 feed"的全局选中——修"关层后右上角详情卡
        // 还挂着该层的点";同时不再无差别清掉别的 feed 的选中(旧 clearSelected 的副作用)。
        void clearSelectionIfOurs()
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(globalSelMutex());
            FeedSelection& s = globalSelStorage();
            if (s.valid && s.sourceId == _spec.id) s = FeedSelection();
        }

        std::string summaryJson() const
        {
            if (_spec.summaryJson) return _spec.summaryJson(pointsOnly());
            // 缺省实现:count + 按 title 首词分桶。
            std::map<std::string, int> byPrefix;
            for (size_t i = 0; i < _records.size(); ++i)
            {
                const std::string& t = _records[i].pt.title;
                std::string prefix = t.substr(0, t.find(' '));
                byPrefix[prefix.empty() ? "?" : prefix]++;
            }
            picojson::object r; r["count"] = picojson::value((double)_records.size());
            picojson::object bucket;
            for (std::map<std::string, int>::iterator it = byPrefix.begin(); it != byPrefix.end(); ++it)
                bucket[it->first] = picojson::value((double)it->second);
            r["byTitlePrefix"] = picojson::value(bucket);
            return picojson::value(r).serialize();
        }

        std::vector<FeedPoint> pointsOnly() const
        {
            std::vector<FeedPoint> pts; pts.reserve(_records.size());
            for (size_t i = 0; i < _records.size(); ++i) pts.push_back(_records[i].pt);
            return pts;
        }

        // P4:区域简报(get_region_brief 跨源合成用)。与 summaryJson 同一线程约定——
        // 主线程调用,直接读 _records 不加锁;遍历**全量原始点**(不用 cluster 聚合后的
        // _levelRecords,区域查询要精确、不按相机高度抽稀)。
        std::string regionSummaryJson(double lat, double lon, double radiusKm) const
        {
            std::vector<std::pair<double, const FeedPoint*> > hits;
            for (size_t i = 0; i < _records.size(); ++i)
            {
                const FeedPoint& p = _records[i].pt;
                double d = haversineKm(lat, lon, p.lat, p.lon);
                if (d <= radiusKm) hits.push_back(std::make_pair(d, &p));
            }
            std::sort(hits.begin(), hits.end(),
                [](const std::pair<double, const FeedPoint*>& a,
                   const std::pair<double, const FeedPoint*>& b) { return a.first < b.first; });
            picojson::object r; r["count"] = picojson::value((double)hits.size());
            picojson::array nearest;
            for (size_t i = 0; i < hits.size() && i < 8; ++i)
            {
                picojson::object n;
                n["title"] = picojson::value(hits[i].second->title);
                n["lat"] = picojson::value(hits[i].second->lat);
                n["lon"] = picojson::value(hits[i].second->lon);
                n["distanceKm"] = picojson::value(floor(hits[i].first * 10.0) * 0.1);
                nearest.push_back(picojson::value(n));
            }
            r["nearest"] = picojson::value(nearest);
            return picojson::value(r).serialize();
        }

        osg::Group* buildScene()
        {
            _root = new osg::Group;
            _root->setNodeMask(0);   // 默认关

            osg::Shader* vs = new osg::Shader(osg::Shader::VERTEX, feedVertCode);
            std::string fsSrc = std::string(earthmark::markerShapeGLSL()) + feedFragMain;
            osg::Shader* fs = new osg::Shader(osg::Shader::FRAGMENT, fsSrc);
            vs->setName(("Feed_" + _spec.id + "_VS").c_str());
            fs->setName(("Feed_" + _spec.id + "_FS").c_str());
            osgVerse::Pipeline::createShaderDefinitions(vs, 100, 130);
            osgVerse::Pipeline::createShaderDefinitions(fs, 100, 130);
            osg::ref_ptr<osg::Program> prog = new osg::Program;
            prog->addShader(vs); prog->addShader(fs);
            _ss = new osg::StateSet;
            _ss->setAttributeAndModes(prog.get(), osg::StateAttribute::ON);
            _ss->setMode(GL_PROGRAM_POINT_SIZE, osg::StateAttribute::ON);
            // 关深度测试:点由 VS 前半球剔除决定可见性(见 feedVertCode),不再依赖深度缓冲——
            // 根治远视角转动时的 z-fight 闪烁。renderBin 11(地表之后)保证正面点画在地表之上。
            _ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
            _ss->setRenderBinDetails(11, "RenderBin");   // 同 quake:地表/海洋 pass 之后画

            _root->addUpdateCallback(new SyncCallback(this));
            return _root;
        }

        const FeedSpec& spec() const { return _spec; }
        osg::Group* root() { return _root; }
        int refreshSeconds() const { return _spec.refreshSeconds > 0 ? _spec.refreshSeconds : 300; }
        // T8:记录的线程安全快照(事件流卡用)。调用方是 ImGui 绘制回调 = draw 线程
        // (DrawThreadPerContext 下与 UPDATE 线程的 syncIfDirty 并发),共用 _mutex
        // 互斥;持锁整份拷贝(每 feed ≤400 条,拷贝廉价,不值得为省它搞无锁结构)。
        std::vector<FeedRecord> snapshotRecords()
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); return _records; }
        bool lastFetchOk() const { return _lastFetchOk; }   // atomic,任意线程可读
        // 任务A:三态抓取状态只读访问器(UI 用它决定"加载中…/抓取失败"指示)。
        int fetchState() const { return _fetchState; }      // atomic,任意线程可读
        std::string lastErrorText() const                   // 加锁拷贝,跨线程安全读字符串
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_errMutex); return _lastErrorText; }
        // 空态指示用:当前已解析要素数(加锁读,仿 lastErrorText 的 const 加锁读)。
        size_t recordCount() const { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); return _records.size(); }

        // ===== P3 聚合 LOD =====
        // 抓取线程侧:对 snap.records 逐级预聚合(单测直调入口)。位置 ecef 与
        // fetchOnce 同款椭球高抬升(_spec.liftMeters);聚合点不做地形矫正(只在高空
        // 显示,liftMeters 量级的抬升 vs 地形误差在千公里视距下不可见,不值得为它跑
        // 求交——前提是 liftMeters 本身设得足够大,盖过目标区域的地表椭球高,见
        // FeedSpec::liftMeters 注释与 correctRecordsToTerrain 的 P3 说明)。
        void buildClusterLevels(FeedSnapshot& snap)
        {
            snap.levels.clear();
            if (_spec.cluster.empty()) return;
            std::vector<FeedPoint> rawPts; rawPts.reserve(snap.records.size());
            for (size_t i = 0; i < snap.records.size(); ++i) rawPts.push_back(snap.records[i].pt);
            for (size_t li = 0; li < _spec.cluster.levels.size(); ++li)
            {
                snap.levels.push_back(std::vector<FeedRecord>());
                double cell = _spec.cluster.levels[li].cellDeg;
                if (cell <= 0.0) continue;   // 原始点级别:空,显示时用 records
                std::vector<FeedPoint> agg = clusterFeedPoints(rawPts, cell, _spec.cluster.makeAggregate);
                std::vector<FeedRecord>& lvl = snap.levels.back(); lvl.reserve(agg.size());
                for (size_t k = 0; k < agg.size(); ++k)
                {
                    FeedRecord r; r.pt = agg[k];
                    r.ecef = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
                        osg::DegreesToRadians(agg[k].lat), osg::DegreesToRadians(agg[k].lon), _spec.liftMeters));
                    lvl.push_back(r);
                }
            }
        }
        // 主线程:按相机高度(km)选择应显级别并切 nodeMask。级别表按 maxCameraAltKm
        // 升序,取第一个 maxCameraAltKm > camAltKm 的;都不满足取最后一级。
        void updateActiveLevel(double camAltKm)
        {
            if (_spec.cluster.empty() || _levelGeodes.empty()) return;
            int want = (int)_spec.cluster.levels.size() - 1;
            for (size_t i = 0; i < _spec.cluster.levels.size(); ++i)
                if (camAltKm < _spec.cluster.levels[i].maxCameraAltKm) { want = (int)i; break; }
            if (want == _activeLevel) return;
            _activeLevel = want;
            for (size_t i = 0; i < _levelGeodes.size(); ++i)
                if (_levelGeodes[i].valid())
                    _levelGeodes[i]->setNodeMask(((int)i == _activeLevel) ? ~0u : 0u);
        }
        int activeLevelIndex() const { return _activeLevel; }
        size_t levelGeodeCount() const { return _levelGeodes.size(); }

        // 主线程逐帧:从 viewer 相机取眼点高度换算 km 喂给 updateActiveLevel。
        // cluster 空 / 无 viewer(单测)时直接返回,零开销。
        void updateActiveLevelFromViewer()
        {
            if (_spec.cluster.empty() || !_viewer || !_viewer->getCamera()) return;
            osg::Vec3d eye = _viewer->getCamera()->getInverseViewMatrix().getTrans();
            osg::Vec3d lla = osgVerse::Coordinate::convertECEFtoLLA(eye);   // (latRad, lonRad, altM)
            updateActiveLevel(lla[2] / 1000.0);
        }

    protected:
        virtual ~FeedLayerImpl()
        {
            if (_thread) { _thread->cancel(); _thread->join(); delete _thread; _thread = nullptr; }
            // T8:从全局注册表自摘(见 globalImpls() 注释)
            std::vector<FeedLayerImpl*>& v = globalImpls();
            v.erase(std::remove(v.begin(), v.end(), this), v.end());
        }

        FeedSpec _spec;
        int _shapeId;   // 该层形状(从 visualForLayer(spec.id) 取,单一真源)
        osg::Group* _root;
        osgViewer::Viewer* _viewer;   // T9:不持有,仅供 syncIfDirty() 地形高度查询(见构造函数注释)
        osg::ref_ptr<osg::Geode> _geode;       // 点(可拾取)
        // P3 聚合 LOD:cluster 非空时启用——每级一个 Geode(同一 _ss),_levelRecords
        // 与 spec.cluster.levels 对齐(原始级为空、显示/拾取用 _records)。
        std::vector<osg::ref_ptr<osg::Geode> > _levelGeodes;
        std::vector<std::vector<FeedRecord> > _levelRecords;
        int _activeLevel = -1;
        osg::ref_ptr<osg::Group> _geomGroup;   // 线/弧几何(不可拾取,整组替换)
        osg::ref_ptr<osg::StateSet> _ss;
        // 6a:线/弧几何的 StateSet/Program,实例内跨 sync 复用(懒创建,主线程)。
        osg::ref_ptr<osg::StateSet> _arcGeomSS, _lineGeomSS;
        std::vector<FeedRecord> _records;
        FeedSnapshot _pending;
        mutable OpenThreads::Mutex _mutex;
        // T1 复审carry-forward:_enabled 由主线程(setEnabled/UI)写、抓取线程(isEnabled)读,
        // 跨线程无锁访问普通 bool 是数据竞争;_dirty 只在 postSnapshot(抓取线程写)/
        // syncIfDirty(主线程读写,持 _mutex)间用,已有锁保护,不必 atomic。
        std::atomic<bool> _enabled;
        // T8:最近一次抓取是否成功(fetchOnce 每轮写;抓取线程写、主线程 feedHealth 读)。
        std::atomic<bool> _lastFetchOk;
        // 任务A:三态抓取状态(0=未抓/idle,1=ok,2=失败)——UI 图层目录用它画"加载中…/
        // 抓取失败"指示,区分"还没抓过"与"抓过但失败了"(_lastFetchOk 只有两态,不够)。
        // fetchOnce 每轮写(抓取线程),EarthControlUI 每帧读(主线程)→ atomic。
        std::atomic<int> _fetchState;
        // 失败时的可读错误文案(fetchOnce 抓取线程写、主线程 UI 读取展示/tooltip)——
        // std::string 跨线程访问必须加锁,不能像 bool/int 那样裸读,故配一把独立小锁,
        // 不与 _mutex(守护 _records/_pending,draw 线程也会碰)混用,避免无谓的锁竞争。
        mutable OpenThreads::Mutex _errMutex;
        std::string _lastErrorText;
        bool _dirty;
        FetchThread* _thread;
    };

    void SyncCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        _owner->syncIfDirty();
        _owner->updateActiveLevelFromViewer();
        traverse(node, nv);
    }

    void FetchThread::run()
    {
        const int kTickMs = 100;
        int intervalTicks = (_owner->refreshSeconds() * 1000) / kTickMs;
        int tick = 0;
        while (!_done)
        {
            if (_owner->isEnabled())
            {
                if (tick <= 0) { _owner->postSnapshot(_owner->fetchOnce()); tick = intervalTicks; }
                else tick--;
            }
            else tick = 0;   // 关闭时,下次开启立即抓(同 quake)
            OpenThreads::Thread::microSleep(kTickMs * 1000);
        }
        _done = true;
    }

    class FeedPickHandler : public osgGA::GUIEventHandler
    {
    public:
        FeedPickHandler(FeedLayerImpl* o) : _owner(o), _downX(0.0f), _downY(0.0f), _pushed(false) {}
        virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
        {
            if (ea.getEventType() == osgGA::GUIEventAdapter::PUSH
                && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            { _downX = ea.getX(); _downY = ea.getY(); _pushed = true; }
            else if (ea.getEventType() == osgGA::GUIEventAdapter::RELEASE
                     && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            {
                float dx = ea.getX() - _downX, dy = ea.getY() - _downY;
                // T1 复审carry-forward:鼠标落在 ImGui 面板(如详情卡/图层目录)上时不拾取
                // 场景要素——否则点面板里的按钮/关闭图标会被误判成"点空白处",拾取穿透
                // 到点下方的地图要素。
                bool overImGui = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
                if (_pushed && dx * dx + dy * dy < kDragThreshPx2 && !overImGui)
                {
                    osgViewer::View* view = static_cast<osgViewer::View*>(&aa);
                    osg::Camera* cam = view->getCamera();
                    const osg::Viewport* vp = cam->getViewport();
                    if (vp)
                    {
                        float my = ea.getY();
                        if (ea.getMouseYOrientation() == osgGA::GUIEventAdapter::Y_INCREASING_DOWNWARDS)
                            my = vp->height() - my;
                        _owner->pickAt(cam, ea.getX(), my);
                    }
                }
                _pushed = false;
            }
            return false;   // 不拦截,放行给 manipulator
        }
    protected:
        FeedLayerImpl* _owner; float _downX, _downY; bool _pushed;
    };
} // namespace

// P4:两点大圆距离(球面近似,地球半径 6371km——与既有聚合/拾取代码同一量级精度,
// 区域简报用途不需要椭球精度)。
double haversineKm(double lat1, double lon1, double lat2, double lon2)
{
    const double kR = 6371.0, kD = osg::PI / 180.0;
    double dLat = (lat2 - lat1) * kD, dLon = (lon2 - lon1) * kD;
    double a = sin(dLat * 0.5) * sin(dLat * 0.5) +
               cos(lat1 * kD) * cos(lat2 * kD) * sin(dLon * 0.5) * sin(dLon * 0.5);
    return 2.0 * kR * atan2(sqrt(a), sqrt(1.0 - a));
}

// P4:跨源区域简报 provider 注册表——file-scope static 单例,registerFeedLayer 里挂接
// (见其后追加块),get_region_brief 工具枚举时用(Task 6)。主线程 only,无锁
// (与 globalImpls() 同一先例)。
std::vector<RegionBriefProvider>& regionBriefProviders()
{ static std::vector<RegionBriefProvider> s; return s; }

std::vector<FeedPoint> clusterFeedPoints(const std::vector<FeedPoint>& pts, double cellDeg,
    const std::function<FeedPoint(const std::vector<FeedPoint>&, double, double)>& makeAggregate)
{
    if (pts.empty() || cellDeg <= 0.0) return pts;
    std::map<std::pair<int, int>, std::vector<FeedPoint> > buckets;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        double lon = pts[i].lon;
        while (lon >= 180.0) lon -= 360.0;
        while (lon < -180.0) lon += 360.0;
        double lat = pts[i].lat;
        if (lat > 90.0) lat = 90.0; if (lat < -90.0) lat = -90.0;
        int ix = (int)std::floor((lon + 180.0) / cellDeg);
        int iy = (int)std::floor((lat + 90.0) / cellDeg);
        // 钳制桶下标:lat 的闭区间 [-90,90] 在 lat=90 时会算出越界桶 180,
        // 与 89.95 分家——修计划自带代码的 off-by-one。
        int maxBucket = (int)std::ceil(180.0 / cellDeg) - 1;
        if (iy > maxBucket) iy = maxBucket;
        if (iy < 0) iy = 0;
        buckets[std::make_pair(ix, iy)].push_back(pts[i]);
    }
    std::vector<FeedPoint> out; out.reserve(buckets.size());
    for (std::map<std::pair<int, int>, std::vector<FeedPoint> >::iterator it = buckets.begin();
         it != buckets.end(); ++it)
    {
        std::vector<FeedPoint>& ms = it->second;
        if (ms.size() == 1) { out.push_back(ms[0]); continue; }   // 单点桶原样保留
        double cLat = 0.0, cLon = 0.0, tMax = 0.0;
        for (size_t k = 0; k < ms.size(); ++k)
        { cLat += ms[k].lat; cLon += ms[k].lon; if (ms[k].unixTime > tMax) tMax = ms[k].unixTime; }
        cLat /= (double)ms.size(); cLon /= (double)ms.size();
        FeedPoint a;
        if (makeAggregate) a = makeAggregate(ms, cLat, cLon);
        else
        {
            // 缺省聚合:颜色/大小取"最大成员"(sizePx 为强度代理),大小随计数对数放大。
            size_t big = 0;
            for (size_t k = 1; k < ms.size(); ++k) if (ms[k].sizePx > ms[big].sizePx) big = k;
            a.color = ms[big].color;
            a.sizePx = std::min(26.0f, ms[big].sizePx + 4.0f * log10f((float)ms.size()));
            char buf[64]; snprintf(buf, sizeof(buf), u8"%d 项聚合", (int)ms.size());
            a.title = buf;
            snprintf(buf, sizeof(buf), u8"该区域共 %d 项", (int)ms.size());
            a.detail = buf;
        }
        a.lat = cLat; a.lon = cLon; a.unixTime = tMax;   // 位置/时间恒由框架决定
        out.push_back(a);
    }
    return out;
}

osg::Group* buildFeedGeometryGroup(const FeedGeometry& g, int arcVertexCount)
{
    // 一次性构建路径(旧行为不变):StateSet 不跨调用复用(demo/单测用)。
    osg::ref_ptr<osg::StateSet> arcSS, lineSS;
    return buildFeedGeometryGroup(g, arcVertexCount, arcSS, lineSS);
}

osg::Group* buildFeedGeometryGroup(const FeedGeometry& g, int arcVertexCount,
                                   osg::ref_ptr<osg::StateSet>& arcSS,
                                   osg::ref_ptr<osg::StateSet>& lineSS)
{
    // 纯场景图构造(shader 对象仅字符串组装,不需 GL 上下文),单测直接调用。
    osg::Group* group = new osg::Group;
    group->setName("FeedGeometry");
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    if (!g.arcs.empty())
    {
        // 全部弧合入一个 buildArcGeometry:共享顶点数组,每弧独立 LINE_STRIP primitiveset
        // (见 geo_primitives.h;着色器/StateSet 由它自带,与 globe 管线零交集)。
        std::vector<earthgeo::ArcStrip> strips(g.arcs.size());
        for (size_t i = 0; i < g.arcs.size(); ++i)
        {
            const FeedArc& a = g.arcs[i];
            strips[i].verts = earthgeo::buildArcVertices(a.llaA, a.llaB,
                                                         arcVertexCount, a.heightScale);
            strips[i].colorO = a.colorA; strips[i].colorD = a.colorB;
        }
        osg::Geometry* arcGeom = earthgeo::buildArcGeometry(strips, arcSS.get());
        if (!arcSS.valid()) arcSS = arcGeom->getStateSet();   // 首次:收编自建的,下轮复用
        geode->addDrawable(arcGeom);
    }
    for (size_t i = 0; i < g.lines.size(); ++i)
    {
        const FeedLine& l = g.lines[i];
        osg::Geometry* lineGeom = earthgeo::buildPolylineGeometry(
            earthgeo::buildPolylineVertices(l.latLonDeg, l.liftMeters), l.color, lineSS.get());
        if (!lineSS.valid()) lineSS = lineGeom->getStateSet();   // 首条自建,后续线全部共用
        geode->addDrawable(lineGeom);
    }
    if (geode->getNumDrawables() > 0) { group->addChild(geode.get()); }
    return group;
}

FeedSelection currentFeedSelection()
{ OpenThreads::ScopedLock<OpenThreads::Mutex> lk(globalSelMutex()); return globalSelStorage(); }

void clearFeedSelection()
{ OpenThreads::ScopedLock<OpenThreads::Mutex> lk(globalSelMutex()); globalSelStorage() = FeedSelection(); }

// T8:跨 feed 收集最新事件(契约/线程约定见 feed_layer.h 声明处注释)。
std::vector<TickerEvent> collectRecentEvents(size_t n)
{
    std::vector<TickerEvent> out;
    std::vector<FeedLayerImpl*>& impls = globalImpls();
    for (size_t i = 0; i < impls.size(); ++i)
    {
        FeedLayerImpl* im = impls[i];
        if (!im->isEnabled()) continue;   // 关着的层不进事件流(与球面显示保持一致)
        // 持 _mutex 的整份拷贝(见 snapshotRecords 注释):draw 线程调用,与 UPDATE
        // 线程 syncIfDirty 的 move 赋值互斥,消除 use-after-free。
        std::vector<FeedRecord> rs = im->snapshotRecords();
        for (size_t k = 0; k < rs.size(); ++k)
        {
            const FeedPoint& p = rs[k].pt;
            if (p.unixTime <= 0.0) continue;   // 无时间点排除(unixTime 契约,见头文件)
            TickerEvent e; e.title = p.title; e.sourceId = im->spec().id;
            e.lat = p.lat; e.lon = p.lon; e.unixTime = p.unixTime;
            out.push_back(e);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const TickerEvent& a, const TickerEvent& b) { return a.unixTime > b.unixTime; });
    if (out.size() > n) out.resize(n);
    return out;
}

// T8:状态带健康统计(静态源恒 ok:一次性加载、无"最近抓取"概念,见头文件注释)。
void feedHealth(int& okCount, int& enabledCount)
{
    okCount = 0; enabledCount = 0;
    std::vector<FeedLayerImpl*>& impls = globalImpls();
    for (size_t i = 0; i < impls.size(); ++i)
    {
        if (!impls[i]->isEnabled()) continue;
        enabledCount++;
        if (impls[i]->isStaticSource() || impls[i]->lastFetchOk()) okCount++;
    }
}

// 图层目录 UI 展示态:在三态抓取状态(0=idle/1=ok/2=失败)上,把"成功但 0 要素"
// 细分为 state 3,让"抓取成功当前无数据(非故障)"与"失败/加载中"在面板上一眼可分。
// 通用于所有 FeedLayer 源(都经 registerFeedLayer 接 fetchStatus)。
static int feedDisplayState(int fetchState, size_t recordCount)
{
    if (fetchState == 1 && recordCount == 0) return 3;   // 成功但空
    return fetchState;                                    // 0/1/2 原样透传
}

osg::Node* registerFeedLayer(const FeedSpec& spec, osgViewer::Viewer& viewer,
                             LayerManager* layers, earthai::ToolRegistry* tools)
{
    osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec, &viewer);
    osg::Group* root = impl->buildScene();
    root->setUserData(impl.get());
    globalImpls().push_back(impl.get());   // T8:登记进全局注册表(析构自摘)
    if (impl->isStaticSource())
    {
        // 静态源(T7):注册时一次性读入本地 fixture,不起轮询线程。数据先挂 pending,
        // 图层开启后首帧 update(SyncCallback→syncIfDirty)才上屏——与轮询源同一条消费路径。
        impl->postSnapshot(impl->fetchOnce());
    }
    else if (!spec.url.empty()) { impl->startFetch(); }   // 线程常驻;仅 isEnabled() 时才真正联网/读 fixture
    else std::cout << "[Feed] " << spec.id << " no url & no fixture, fetch disabled\n";
    viewer.addEventHandler(new FeedPickHandler(impl.get()));

    if (layers)
    {
        OverlayLayer l; l.id = spec.id; l.displayName = spec.displayName;
        l.group = spec.group; l.subtitle = spec.subtitle; l.enabled = false; l.hasOpacity = false;
        earthmark::MarkerVisual mv = earthmark::visualForLayer(spec.id);
        l.shape = mv.shape; l.iconColor = mv.color;
        FeedLayerImpl* implPtr = impl.get();
        l.apply = [implPtr](const OverlayLayer& lay) { implPtr->setEnabled(lay.enabled); };
        // 任务A:抓取失败 UI 可见——UI 每帧调它,返回 0/1/2/3(见 OverlayLayer::fetchStatus
        // 注释),2 时把错误文案写进 out 供 tooltip 展示。Task 1:经 feedDisplayState 把
        // "成功但 0 要素"细分为 3(对 2 原样透传,故 if(s==2) 填 errText 的失败分支语义不变)。
        l.fetchStatus = [implPtr](std::string& out) -> int
        {
            int s = earthfeed::feedDisplayState(implPtr->fetchState(), implPtr->recordCount());
            if (s == 2) out = implPtr->lastErrorText();
            return s;
        };
        OverlayLayer& added = layers->add(l);
        // env 强制开关钩子(同 EARTH_QUAKES 语义):forceEnv 非 "0" → 启动即开启。
        if (!spec.forceEnv.empty())
        {
            const char* fEnv = getenv(spec.forceEnv.c_str());
            if (fEnv && *fEnv) added.enabled = (std::string(fEnv) != "0");
        }
        layers->setEnabled(spec.id, added.enabled);
    }

    if (tools)
    {
        earthai::Tool t; t.name = "get_" + spec.id + "_summary";
        t.description = spec.toolDescriptionCn;
        t.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        FeedLayerImpl* implPtr = impl.get();
        LayerManager* layersPtr = layers;
        std::string layerId = spec.id, toolName = t.name;
        t.execute = [implPtr, layersPtr, layerId, toolName](const picojson::value&) {
            if (layersPtr && !implPtr->isEnabled()) layersPtr->setEnabled(layerId, true);
            std::string json = implPtr->summaryJson();
            picojson::value v; std::string perr = picojson::parse(v, json);
            if (!perr.empty() || !v.is<picojson::object>())
            {
                picojson::object err; err["error"] = picojson::value("bad summary json: " + perr);
                return picojson::value(err);
            }
            picojson::object& obj = v.get<picojson::object>();
            double n = obj.count("count") ? obj["count"].get<double>() : 0.0;
            if (n <= 0.0)
            {
                // 静态源(无轮询线程)永远不会有"稍后"——0 条就是 fixture 缺失/为空,
                // 别骗调用方等一个不存在的抓取(评审修复)。
                if (implPtr->hasFetchThread())
                {
                    obj["loadingNote"] = picojson::value(
                        std::string(u8"图层已自动开启，数据抓取中，请稍后再查询一次"));
                }
                else
                {
                    obj["loadingNote"] = picojson::value(
                        std::string(u8"静态数据未加载（fixture 缺失或为空），该数据源不会自动刷新"));
                }
            }
            OSG_NOTICE << "[Feed] " << toolName << " count=" << (long long)n << std::endl;
            return picojson::value(obj);
        };
        tools->add(t);
    }

    // P4:注册区域简报 provider(get_region_brief 跨源合成用)。独立于 tools 是否为空——
    // 没有 AI key 时 tools 可能是 nullptr,但 get_region_brief 仍要能枚举 provider(它是
    // 底层基建,不依赖单个 feed 的 get_<id>_summary 工具是否注册)。implPtr 生命周期同上注释。
    {
        RegionBriefProvider bp; bp.id = spec.id;
        FeedLayerImpl* rp = impl.get();
        bp.isEnabled = [rp]() { return rp->isEnabled(); };
        bp.regionSummaryJson = [rp](double la, double lo, double rk)
        { return rp->regionSummaryJson(la, lo, rk); };
        regionBriefProviders().push_back(bp);
    }
    return root;
}

} // namespace earthfeed
