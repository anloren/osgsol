#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Point>
#include <osg/NodeCallback>
#include <osgDB/FileUtils>
#include <OpenThreads/Thread>
#include <OpenThreads/Mutex>
#include <atomic>
#include <modeling/Math.h>
#include <pipeline/Pipeline.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include "3rdparty/libhv/all/client/requests.h"
#include "3rdparty/sgp4/DateTime.h"
#include "3rdparty/sgp4/Tle.h"
#include "sat_math.h"
#include "sat_data.h"
#include "geo_primitives.h"
#include "marker_style.h"
#include <osgGA/GUIEventHandler>

namespace
{
    static const double kCacheTtlSeconds = 86400.0;   // 24h,遵守 CelesTrak 使用建议

    struct Satellite
    {
        std::string name, line1, line2;
        int noradId = 0;
        SatCategory category = SatCategory::Station;
        double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
        osg::Vec3d ecef;
        osg::Vec3d ecefVelocity;   // 米/秒,由相邻两次后台传播的 ECEF 差值/时间差算出
        bool hasEcef = false;   // true once ecef has been set by a real propagation (guards against computing velocity from an uninitialized/origin ecef on the first repropagate)
        double lastUpdateRefTime = 0.0;   // 该卫星最近一次被传播时的 viewer referenceTime
    };

    int noradFromLine1(const std::string& line1)
    {
        if (line1.size() < 7) return 0;
        return atoi(line1.substr(2, 5).c_str());
    }

    // 磁盘缓存根目录:不能沿用 mainFolder(= Contents/models,在 .app 包内部)——
    // packaging/package_macos.sh 每次打包都 `rm -rf` 整个旧 .app 再重建,包内文件必然
    // 清空,写在这里的缓存活不过下一次打包(2026-07-05 真机反馈"明明拉取成功过,缓存却
    // 不见了"就是这个原因)。改用和既有瓦片缓存(readerwriter/UtilitiesEx.cpp
    // tileCacheDir())完全同一套惯例:默认 $HOME/Library/Caches/EarthExplorer/sat_cache
    // (用户目录下,重装/重新打包都不受影响),EARTH_SAT_CACHE 可覆盖路径,=0 显式关闭
    // 磁盘缓存(退化为每次都联网,不写不读)。
    std::string satCacheDir()
    {
        static std::string sDir = []() -> std::string {
            const char* env = getenv("EARTH_SAT_CACHE");
            std::string dir;
            if (env && env[0])
            {
                if (strcmp(env, "0") == 0) return std::string();
                dir = env;
            }
            else
            {
                const char* home = getenv("HOME");
                if (home == NULL || home[0] == '\0') return std::string();   // 无 HOME 不猜相对路径
                dir = std::string(home) + "/Library/Caches/EarthExplorer/sat_cache";
            }
            if (!osgDB::makeDirectory(dir))
            {
                std::cout << "[Sat] failed to create disk cache root " << dir << ", disk cache disabled\n";
                return std::string();
            }
            std::cout << "[Sat] disk cache at " << dir << std::endl;
            return dir;
        }();
        return sDir;
    }

    // 磁盘缓存新鲜度:sidecar 时间戳文件(纯文本 epoch 秒),避免依赖平台相关的文件
    // mtime API。纯函数,不碰文件系统之外的状态,可单独验证逻辑(此处内联,未单测——
    // 逻辑足够简单,行为由 Step 5 的 E2E fixture 路径间接覆盖)。
    bool cacheIsFresh(double cachedEpoch, double nowEpoch) { return (nowEpoch - cachedEpoch) < kCacheTtlSeconds; }

    std::string readWholeFile(const std::string& path)
    {
        std::ifstream in(path); if (!in) return "";
        std::stringstream ss; ss << in.rdbuf(); return ss.str();
    }

    // 磁盘缓存 + 网络抓取一个 CelesTrak GROUP(不含 fixture 分支,调用方负责 fixture 短路)。
    // errDetail 非空且最终确实一无所获(网络失败 + 没有旧缓存可退)时,把服务器原始返回文本
    // 透传出去——比如 CelesTrak 限流时的 403 正文会明确写"上次成功下载是几点、多久更新一次",
    // 这条信息服务器自己给得比我们瞎猜"稍后重试"准确得多,直接抄原文,换行折成空格方便单行
    // UI 显示,不用自己翻译/复述(服务器措辞以后变了也不会跟着过时)。
    std::string fetchGroupTextNetwork(const std::string& celestrakGroup, const std::string& cacheDir,
                                      std::string* errDetail = nullptr)
    {
        // 空 cacheDir = 磁盘缓存被显式关闭(EARTH_SAT_CACHE=0)或 HOME 未设——不猜路径,
        // 直接跳过读写缓存,退化成"每次都联网、失败就是失败"(不去拼一个 "/xxx.tle" 这种
        // 无主目录的可疑路径出来读写)。
        bool cacheEnabled = !cacheDir.empty();
        std::string cacheFile = cacheEnabled ? (cacheDir + "/" + celestrakGroup + ".tle") : std::string();
        std::string tsFile = cacheFile.empty() ? std::string() : (cacheFile + ".ts");
        double now = (double)time(nullptr);
        if (cacheEnabled)
        {
            std::ifstream tsIn(tsFile);
            double cachedEpoch = -1.0;
            if (tsIn && (tsIn >> cachedEpoch) && cacheIsFresh(cachedEpoch, now))
            {
                std::string cached = readWholeFile(cacheFile);
                if (!cached.empty()) return cached;
            }
        }
        std::string url = "https://celestrak.org/NORAD/elements/gp.php?GROUP=" + celestrakGroup + "&FORMAT=tle";
        requests::Request req(new HttpRequest);
        req->method = HTTP_GET; req->url = url; req->timeout = 20;
        requests::Response resp = requests::request(req);
        if (!resp || resp->status_code != 200)
        {
            std::cout << "[Sat] network fetch failed group=" << celestrakGroup
                       << " status=" << (resp ? (int)resp->status_code : -1) << "\n";
            std::string stale = readWholeFile(cacheFile);   // 网络失败兜底:过期缓存也比没有强
            if (stale.empty() && errDetail)
            {
                std::string raw = (resp && !resp->body.empty()) ? resp->body : std::string(u8"网络连接失败");
                for (size_t i = 0; i < raw.size(); ++i) if (raw[i] == '\n' || raw[i] == '\r') raw[i] = ' ';
                // 折行后常见"行尾空格 + 折出来的空格"连续两个空格,合并成一个再显示。
                std::string collapsed; collapsed.reserve(raw.size());
                for (size_t i = 0; i < raw.size(); ++i)
                    if (raw[i] != ' ' || collapsed.empty() || collapsed.back() != ' ') collapsed.push_back(raw[i]);
                while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
                *errDetail = collapsed;
            }
            return stale;
        }
        if (cacheEnabled)
        {
            osgDB::makeDirectory(cacheDir);
            std::ofstream out(cacheFile); if (out) out << resp->body;
            std::ofstream tsOut(tsFile); if (tsOut) tsOut << now;
        }
        return resp->body;
    }

    struct GroupSpec { const char* celestrakGroup; SatCategory category; };
    const GroupSpec kPreciseGroups[] = {
        { "stations",    SatCategory::Station },
        { "gps-ops",     SatCategory::Navigation },
        { "beidou",      SatCategory::Navigation },
        { "galileo",     SatCategory::Navigation },
        { "glonass-ops", SatCategory::Navigation },
        { "weather",     SatCategory::Weather },
    };

    std::vector<Satellite> entriesToSatellites(const std::vector<earthsat::TleEntry>& entries, SatCategory cat)
    {
        std::vector<Satellite> out; out.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
        {
            Satellite s; s.name = entries[i].name; s.line1 = entries[i].line1; s.line2 = entries[i].line2;
            s.category = cat; s.noradId = noradFromLine1(entries[i].line1);
            out.push_back(s);
        }
        return out;
    }

    // 传播一批卫星的当前 ECI/地心状态,写回 lat/lon/alt/speed/ecef,并用与上一次
    // ecef 的差值算出 ecefVelocity(米/秒)供主线程逐帧外推。now 是 DateTime::Now()
    // 与各自 TLE 历元的分钟差——每颗卫星历元不同,分别计算。
    void repropagate(std::vector<Satellite>& sats, double prevUpdateEpochSeconds, double nowEpochSeconds)
    {
        double dt = nowEpochSeconds - prevUpdateEpochSeconds; if (dt <= 0.0) dt = 1.0;
        for (size_t i = 0; i < sats.size(); ++i)
        {
            Satellite& s = sats[i];
            libsgp4::DateTime nowDt = libsgp4::DateTime::Now();
            double tsince = 0.0;
            try
            {
                libsgp4::Tle tle(s.line1, s.line2);
                tsince = (nowDt - tle.Epoch()).TotalMinutes();
            }
            catch (const std::exception&) { continue; }
            earthsat::PropagatedState st = earthsat::propagateOne(s.line1, s.line2, tsince);
            if (!st.valid) continue;
            osg::Vec3d newEcef = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
                st.latDeg * (M_PI / 180.0), st.lonDeg * (M_PI / 180.0), st.altKm * 1000.0));
            // 首次传播:ecef 还是构造时的 (0,0,0)(地心),用它算出的"速度"是几百万 m/s 的
            // 垃圾值(会让卫星在下一次真正传播到来前的这一秒内飞出地球几千公里再弹回来)。
            // 首次只播种位置、速度置零,从第二次传播起才开始算真实的位置差分速度。
            s.ecefVelocity = s.hasEcef ? (newEcef - s.ecef) / dt : osg::Vec3d(0.0, 0.0, 0.0);
            s.ecef = newEcef; s.hasEcef = true;
            s.latDeg = st.latDeg; s.lonDeg = st.lonDeg;
            s.altKm = st.altKm; s.speedKmS = st.speedKmS;
        }
    }

    // 精选组(空间站+导航+气象,6 个 CelesTrak GROUP 合并):EARTH_SATS_FILE 设置时整体
    // 短路(全部标 Station 类目,测试用,不区分子类目——真实网络路径每组各自打对应类目)。
    std::vector<Satellite> fetchPreciseSatellites(const std::string& cacheDir)
    {
        const char* fixtureFile = getenv("EARTH_SATS_FILE");
        if (fixtureFile && *fixtureFile)
        {
            std::string text = readWholeFile(fixtureFile);
            if (text.empty()) { std::cout << "[Sat] EARTH_SATS_FILE open failed: " << fixtureFile << "\n"; return {}; }
            std::vector<Satellite> out = entriesToSatellites(earthsat::parseTleBlock(text), SatCategory::Station);
            std::cout << "[Sat] Parsed " << out.size() << " satellites (precise, fixture)\n";
            return out;
        }
        std::vector<Satellite> all;
        for (size_t i = 0; i < sizeof(kPreciseGroups) / sizeof(kPreciseGroups[0]); ++i)
        {
            std::string text = fetchGroupTextNetwork(kPreciseGroups[i].celestrakGroup, cacheDir);
            std::vector<Satellite> group = entriesToSatellites(earthsat::parseTleBlock(text), kPreciseGroups[i].category);
            all.insert(all.end(), group.begin(), group.end());
        }
        std::cout << "[Sat] Parsed " << all.size() << " satellites (precise, network, "
                   << (sizeof(kPreciseGroups) / sizeof(kPreciseGroups[0])) << " groups)\n";
        return all;
    }

    std::vector<Satellite> fetchStarlinkSatellites(const std::string& cacheDir, std::string* errDetail = nullptr)
    {
        const char* fixtureFile = getenv("EARTH_STARLINK_FILE");
        std::string text;
        bool fromFixture = (fixtureFile && *fixtureFile);
        if (fromFixture)
        {
            text = readWholeFile(fixtureFile);
            if (text.empty()) { std::cout << "[Sat] EARTH_STARLINK_FILE open failed: " << fixtureFile << "\n"; return {}; }
        }
        else text = fetchGroupTextNetwork("starlink", cacheDir, errDetail);
        std::vector<Satellite> out = entriesToSatellites(earthsat::parseTleBlock(text), SatCategory::Starlink);
        std::cout << "[Sat] Parsed " << out.size() << " satellites (starlink, "
                   << (fromFixture ? "fixture" : "network") << ")\n";
        return out;
    }

    // 点精灵着色器:VS 不变(卫星不需要航向旋转,也没有前半球剔除段——本模块从未加过
    // 该段,与 flight/ais 不同,这里没有东西可"保留",维持原样)。FS 形状绘制改用
    // earthmark::markerShapeGLSL() 共享库(marker T3),固定 SatBox(shapeId=9,不旋转)。
    // 逐模块各自持有一份小着色器是本仓库既有惯例(flight/feed 均如此),不共享全局
    // Program(避免 P1 已踩过的全局 static 共享 Program 导致瓦片染色的坑)。
    const char* satVertCode = {
        "VERSE_VS_OUT vec4 pointColor;\n"
        "void main() {\n"
        "    pointColor = osg_Color;\n"
        "    gl_PointSize = osg_MultiTexCoord0.x;\n"
        "    gl_Position = VERSE_MATRIX_MVP * osg_Vertex;\n"
        "}\n"
    };
    // FS 入口:实际形状绘制逻辑在 earthmark::markerShapeGLSL() 共享库里(buildScene()
    // 拼接在本字符串之前,提供 markerCoverage)——卫星固定 SatBox(shapeId=9),不旋转。
    // ISS/天宫高亮(更大尺寸+青白色)在 buildSatGeode() 的顶点属性/颜色里决定,与本 FS 无关。
    const char* satFragMain = {
        "VERSE_FS_IN vec4 pointColor;\n"
        "#ifdef VERSE_GLES3\n"
        "layout(location = 0) VERSE_FS_OUT vec4 fragColor;\n"
        "layout(location = 1) VERSE_FS_OUT vec4 fragOrigin;\n"
        "#endif\n"
        "void main() {\n"
        "    float cov = markerCoverage(9, gl_PointCoord, 0.0);\n"
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

    osg::Vec4 categoryColor(SatCategory cat)
    {
        switch (cat)
        {
        case SatCategory::Station:    return osg::Vec4(1.00f, 0.85f, 0.20f, 1.0f);  // 空间站:金黄
        case SatCategory::Navigation: return osg::Vec4(0.35f, 0.95f, 0.55f, 1.0f);  // 导航:绿
        case SatCategory::Weather:    return osg::Vec4(0.55f, 0.75f, 1.00f, 1.0f);  // 气象:蓝
        default:                     return osg::Vec4(0.55f, 0.55f, 0.60f, 1.0f);  // Starlink:暗灰壳层
        }
    }
    static const float kSatSizePx = 9.0f;
    static const float kStarlinkSizePx = 3.0f;
    static const float kAlwaysShowSizePx = 18.0f;   // ISS/天宫专属:比普通"空间站"类目大一倍,一眼能认出来
    // 亮青白色,与常显轨道线同色系,区别于"空间站"类目里 21 个碎片/学生立方星/对接飞船共用的金黄色。
    // 写成函数而非文件作用域 static osg::Vec4——避免非 POD 静态对象的初始化顺序问题(同 categoryColor()
    // 的既有写法:颜色值一律通过函数返回,不做文件作用域静态存储)。
    osg::Vec4 alwaysShowColor() { return osg::Vec4(0.55f, 1.00f, 1.00f, 1.0f); }

    // ISS / 天宫核心舱(CSS TIANHE):真机验收(2026-07-05)发现真实 CelesTrak "stations" 分组
    // 除这两个真正的空间站外,还混了对接飞船(Crew Dragon/Progress/Cygnus/Soyuz)、空间站舱段
    // (POISK/NAUKA/WENTIAN/MENGTIAN/SZ-21)、火箭残骸(FREGAT DEB)、ISS 释放的学生立方星
    // (HMU-SAT2/KNACKSAT-2/CORAL/...)共 23 个物体——这是 CelesTrak 官方分组口径本身如此,
    // 不是本模块分类错误。这 23 个都保留在「空间站」类目里(用户确认接受),但 ISS/天宫单独
    // 放大+换色高亮,不然混在一堆同色点里认不出来。
    bool isAlwaysShowSatellite(int noradId) { return noradId == 25544 || noradId == 48274; }

    osg::Geode* buildSatGeode(const std::vector<Satellite>& sats, osg::StateSet* sharedSS, float sizePx)
    {
        osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
        osg::ref_ptr<osg::Vec2Array> sizes = new osg::Vec2Array;   // 同 feed_layer.cpp:Vec2,只用 .x
        for (size_t i = 0; i < sats.size(); ++i)
        {
            verts->push_back(sats[i].ecef);
            bool highlight = isAlwaysShowSatellite(sats[i].noradId);
            colors->push_back(highlight ? alwaysShowColor() : categoryColor(sats[i].category));
            sizes->push_back(osg::Vec2(highlight ? kAlwaysShowSizePx : sizePx, 0.0f));
        }
        geom->setVertexArray(verts.get());
        geom->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
        geom->setTexCoordArray(0, sizes.get());
        geom->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, (GLsizei)verts->size()));
        geom->setUseDisplayList(false); geom->setUseVertexBufferObjects(true);
        geom->setCullingActive(false);
        // interpolateOne() 每 UPDATE 帧改写 verts 并 dirty();多线程绘制(DrawThreadPerContext)下
        // draw 与下一帧 update 之间没有隐式屏障，需显式标 DYNAMIC 让 OSG 加锁同步，否则数据竞争+撕裂。
        // colors/sizes 只在 buildSatGeode 构建时写一次、之后不再改，故不需要标。
        geom->setDataVariance(osg::Object::DYNAMIC);
        verts->setDataVariance(osg::Object::DYNAMIC);
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(geom.get()); geode->setStateSet(sharedSS);
        return geode.release();
    }

    class SatelliteLayerImpl;

    class FetchThread : public OpenThreads::Thread
    {
    public:
        FetchThread(SatelliteLayerImpl* o) : _owner(o), _done(false) {}
        virtual int cancel() { _done = true; return OpenThreads::Thread::cancel(); }
        virtual void run();
    protected:
        SatelliteLayerImpl* _owner; std::atomic<bool> _done;   // cancel() 主线程写 / run() 抓取线程读,跨线程
    };

    class SyncCallback : public osg::NodeCallback
    {
    public:
        SyncCallback(SatelliteLayerImpl* o) : _owner(o) {}
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);
    protected:
        SatelliteLayerImpl* _owner;
    };

    class SatelliteLayerImpl : public osg::Referenced, public SatelliteLayer
    {
    public:
        // mainFolder(= Contents/models,.app 包内部路径)不再用来定位缓存目录——见
        // satCacheDir() 的注释;仍从 configureSatelliteLayer 传入是为了保持和
        // configureFlightLayer/configure3DTilesLayer 等既有同类函数一致的公开签名,
        // 万一以后真的需要 mainFolder 下的东西(比如离线 TLE 种子文件)不用改接口。
        SatelliteLayerImpl()
            : _cacheDir(satCacheDir()), _root(0),
              _catStation(false), _catNav(false), _catWeather(false), _catStarlink(false),
              _preciseFetchTriggered(false), _starlinkFetchTriggered(false),
              _preciseFetchDone(false), _starlinkFetchDone(false),
              _preciseDirty(false), _starlinkDirty(false), _rebuildNeeded(false),
              _thread(nullptr) {}

        virtual void setCategoryEnabled(SatCategory cat, bool on)
        {
            bool changed = false;
            switch (cat)
            {
            case SatCategory::Station:    changed = (_catStation != on); _catStation = on; break;
            case SatCategory::Navigation: changed = (_catNav != on);     _catNav = on; break;
            case SatCategory::Weather:    changed = (_catWeather != on); _catWeather = on; break;
            case SatCategory::Starlink:   changed = (_catStarlink != on); _catStarlink = on; break;
            }
            if (!changed) return;
            if (!on)
            {
                SatelliteInfo selected = getSelected();
                if (selected.valid && selected.category == cat) clearSelected();
            }
            if (cat == SatCategory::Station || cat == SatCategory::Navigation ||
                cat == SatCategory::Weather)
            {
                if (on) _preciseFetchTriggered = true;
                // 懒加载:任一精选类目首次开启才联网。
                bool categoryHasData = false;
                for (size_t i = 0; i < _allPrecise.size(); ++i)
                    if (_allPrecise[i].category == cat) { categoryHasData = true; break; }
                if (earthsat::shouldRequestPreciseRefetch(on, _preciseFetchDone, categoryHasData))
                    _preciseRefetchRequested = true;
            }
            if (cat == SatCategory::Starlink && on)
            {
                _starlinkFetchTriggered = true;  // Starlink 独立懒加载(同 hk3d 模式)
                // 上次尝试已经完成但一颗都没拿到(网络失败/CelesTrak 限流)——重新打开开关时
                // 真的再请求一次网络,不然 fetchErrorText() 的"稍后重试"提示会是空话。只对
                // "确认失败过"的情况重试,已有数据时重新打开不应该无谓地再打一次网络请求。
                if (_starlinkFetchDone && _allStarlink.empty())
                    _starlinkRefetchRequested = true;
            }
            _rebuildNeeded = true;
            if (_starlinkRoot.valid()) _starlinkRoot->setNodeMask(_catStarlink ? ~0u : 0u);
        }
        virtual bool isCategoryEnabled(SatCategory cat) const
        {
            switch (cat)
            {
            case SatCategory::Station:    return _catStation;
            case SatCategory::Navigation: return _catNav;
            case SatCategory::Weather:    return _catWeather;
            default:                      return _catStarlink;
            }
        }
        virtual void selectByNoradId(int noradId) { selectByNoradIdInternal(noradId); }
        virtual void clearSelected()
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex);
            _selected = SatelliteInfo();
            _selectedOrbitDirty = true;   // 下一帧清空选中态的轨道线/足迹圆几何
        }
        virtual SatelliteInfo getSelected() const
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex); return _selected; }
        // 主线程读(AI drainMainThread 是主线程 FRAME handler);_allPrecise/_allStarlink
        // 也只在主线程 syncIfDirty() 写——同 flight_data.cpp summaryJson 约定,不加锁。
        // 只做只读汇总,不碰抓取/传播/渲染(红线)。
        virtual std::string summaryJson() const
        {
            std::vector<earthsat::SatSummaryEntry> entries;
            entries.reserve(_allPrecise.size() + _allStarlink.size());
            for (size_t i = 0; i < _allPrecise.size(); ++i)
            {
                earthsat::SatSummaryEntry e;
                e.noradId = _allPrecise[i].noradId; e.category = (int)_allPrecise[i].category;
                e.latDeg = _allPrecise[i].latDeg;   e.lonDeg = _allPrecise[i].lonDeg;
                e.altKm = _allPrecise[i].altKm;     e.speedKmS = _allPrecise[i].speedKmS;
                entries.push_back(e);
            }
            for (size_t i = 0; i < _allStarlink.size(); ++i)
            {
                earthsat::SatSummaryEntry e;
                e.noradId = _allStarlink[i].noradId; e.category = (int)_allStarlink[i].category;
                e.latDeg = _allStarlink[i].latDeg;   e.lonDeg = _allStarlink[i].lonDeg;
                e.altKm = _allStarlink[i].altKm;     e.speedKmS = _allStarlink[i].speedKmS;
                entries.push_back(e);
            }
            return earthsat::buildSatelliteSummaryJson(entries);
        }

        // _allPrecise/_allStarlink/_preciseFetchDone/_starlinkFetchDone/_starlinkErrorDetail
        // 全部只在主线程的 syncIfDirty() 里写(消费快照的同一处),本方法也只应从主线程调用
        // (EarthControlUI 的每帧 UI 绘制 / earth_main.cpp 的状态轮询 handler)——两边天然
        // 同步,不需要加锁,也不会重蹈 lastUpdateRefTime 那次跨线程时序竞态的覆辙。
        virtual std::string fetchErrorText(SatCategory cat) const
        {
            // 措辞不写"稍后自动重试"——本模块没有后台自动重试机制(CelesTrak 明确要求
            // 不要频繁重复请求同一分组,不适合做定时轮询重试);四个类目都支持关闭再打开
            // 手动重试一次(见 setCategoryEnabled 的 refetch-request 逻辑),但不会定时重试。
            static const char* kFailText = u8"本次未拉取到数据(可尝试关闭再打开重试)";
            if (cat == SatCategory::Starlink)
            {
                if (!_catStarlink || !_starlinkFetchDone) return "";
                if (_allStarlink.empty())
                    // 优先透出服务器原话(比如 CelesTrak 限流会说清楚"什么时候能重试"),
                    // 没有具体原因(比如 fixture 路径/纯网络超时无响应体)才用通用措辞兜底。
                    return _starlinkErrorDetail.empty() ? kFailText : (u8"CelesTrak: " + _starlinkErrorDetail);
                return "";
            }
            bool enabled = false;
            switch (cat)
            {
            case SatCategory::Station:    enabled = _catStation; break;
            case SatCategory::Navigation: enabled = _catNav; break;
            case SatCategory::Weather:    enabled = _catWeather; break;
            default: return "";
            }
            if (!enabled || !_preciseFetchDone) return "";
            for (size_t i = 0; i < _allPrecise.size(); ++i)
                if (_allPrecise[i].category == cat) return "";
            return kFailText;
        }

        // 屏幕拾取:相机 + 窗口鼠标坐标(y 向上)。选最近的前半球卫星(精选组)。仅主线程调用。
        void pickAt(osg::Camera* cam, float mx, float my, double refTime)
        {
            if (!cam || _visiblePrecise.empty() || !cam->getViewport())
            {
                clearSelected();
                return;
            }
            osg::Vec3d eye, center, up; cam->getViewMatrixAsLookAt(eye, center, up);
            osg::Matrixd VPW = cam->getViewMatrix() * cam->getProjectionMatrix()
                             * cam->getViewport()->computeWindowMatrix();
            double bestD2 = 1e18; int best = -1;
            for (size_t i = 0; i < _visiblePrecise.size(); ++i)
            {
                osg::Vec3d P = earthsat::extrapolateSatelliteEcef(
                    _visiblePrecise[i].ecef, _visiblePrecise[i].ecefVelocity,
                    _visiblePrecise[i].lastUpdateRefTime, refTime);
                if ((eye * P) <= (P * P)) continue;   // 前半球
                osg::Vec3d win = P * VPW;
                double d2 = (win.x()-mx)*(win.x()-mx) + (win.y()-my)*(win.y()-my);
                float tol = 12.0f;
                if (d2 < (double)(tol*tol) && d2 < bestD2) { bestD2 = d2; best = (int)i; }
            }
            if (best >= 0) selectByNoradIdInternal(_visiblePrecise[best].noradId);
            else clearSelected();
        }

        bool preciseFetchTriggered() const { return _preciseFetchTriggered; }
        bool starlinkFetchTriggered() const { return _starlinkFetchTriggered; }
        // 取走并清零"请求重新拉取精选组"标记。
        bool takePreciseRefetchRequest() { return _preciseRefetchRequested.exchange(false); }
        // 取走并清零"请求重新拉取 Starlink"标记(读一次即消费,同 takeRefreshNow 类既有惯例)。
        bool takeStarlinkRefetchRequest() { return _starlinkRefetchRequested.exchange(false); }
        const std::string& cacheDir() const { return _cacheDir; }

        void postPreciseSnapshot(const std::vector<Satellite>& sats)
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); _pendingPrecise = sats; _preciseDirty = true; }
        // errDetail:本次拉取一无所获时服务器的原始错误文本(可空,同一把 _mutex 一并交给
        // 主线程,在 syncIfDirty() 消费快照的同一处转存到 _starlinkErrorDetail——不单独加锁)。
        void postStarlinkSnapshot(const std::vector<Satellite>& sats, const std::string& errDetail = "")
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
            _pendingStarlink = sats; _pendingStarlinkErrorDetail = errDetail; _starlinkDirty = true;
        }

        std::vector<Satellite> currentPrecise()
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); return _allPrecise; }
        std::vector<Satellite> currentStarlink()
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); return _allStarlink; }

        // 主线程(update 遍历)调用:有新抓取数据或类目开关变了就重建几何。refTime 是
        // 本帧的 viewer referenceTime,用作 lastUpdateRefTime 基准(供 interpolate() 外推)。
        void syncIfDirty(double refTime)
        {
            bool needRebuild = _rebuildNeeded;
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
                // 新快照到达的这一帧,把这一批卫星的外推基准时刻重置为当前 refTime——
                // repropagate() 算出的 ecef/ecefVelocity 就是"此刻"的状态,从这一帧起
                // interpolate() 才开始按 ecefVelocity 外推。没有新快照的卫星保留原有
                // lastUpdateRefTime,好让 elapsed 逐帧增长,外推才真正生效。
                if (_preciseDirty)
                {
                    _allPrecise = _pendingPrecise; _preciseDirty = false; needRebuild = true;
                    for (size_t i = 0; i < _allPrecise.size(); ++i) _allPrecise[i].lastUpdateRefTime = refTime;
                    // 在消费快照的同一处(主线程)置位,不让后台线程直接设——避免和消费
                    // _allPrecise 之间出现和 lastUpdateRefTime 同一类的跨线程时序竞态
                    // (fetchErrorText() 只从主线程读 _allPrecise,这里保证两者步调一致)。
                    _preciseFetchDone = true;
                }
                if (_starlinkDirty)
                {
                    _allStarlink = _pendingStarlink; _starlinkDirty = false; needRebuild = true;
                    for (size_t i = 0; i < _allStarlink.size(); ++i) _allStarlink[i].lastUpdateRefTime = refTime;
                    _starlinkFetchDone = true;
                    _starlinkErrorDetail = _pendingStarlinkErrorDetail;
                }
            }
            if (_selectedOrbitDirty.exchange(false)) needRebuild = true;

            if (!needRebuild) return;
            _rebuildNeeded = false;

            _visiblePrecise.clear();
            for (size_t i = 0; i < _allPrecise.size(); ++i)
            {
                const Satellite& s = _allPrecise[i];
                if ((s.category == SatCategory::Station && _catStation) ||
                    (s.category == SatCategory::Navigation && _catNav) ||
                    (s.category == SatCategory::Weather && _catWeather))
                    _visiblePrecise.push_back(s);
            }
            // EARTH_SAT_DEBUG=1:每次几何重建时打一行计数(零影响于未设置时)。曾用它抓到
            // 过一个真机才现形的时序 bug(见 FetchThread::run() 注释)——fixture 测试从没
            // 暴露过,留着方便下次遇到"开关变了但地球上看不到卫星"这类问题时快速定位是
            // 卡在"过滤后没数据"还是"数据到了但没画出来"。
            if (getenv("EARTH_SAT_DEBUG"))
                std::cout << "[SatDBG] visiblePrecise=" << _visiblePrecise.size()
                          << " allPrecise=" << _allPrecise.size() << " starlink=" << _allStarlink.size()
                          << " cat(station/nav/weather/starlink)=" << _catStation << "/" << _catNav << "/"
                          << _catWeather << "/" << _catStarlink << std::endl;
            if (_preciseGeode.valid()) _preciseRoot->removeChild(_preciseGeode.get());
            _preciseGeode = buildSatGeode(_visiblePrecise, _ss.get(), kSatSizePx);
            _preciseRoot->addChild(_preciseGeode.get());

            if (_starlinkGeode.valid()) _starlinkRoot->removeChild(_starlinkGeode.get());
            _starlinkGeode = buildSatGeode(_allStarlink, _ss.get(), kStarlinkSizePx);
            _starlinkRoot->addChild(_starlinkGeode.get());

            // 精选组每 1s 重新传播时也重算当前选中轨道;点选/取消选中的原子脏标记则让
            // 交互在当帧强制 needRebuild=true,不用等待下一次精选组数据到达。
            rebuildOrbitLines();
        }

        void interpolate(double refTime)
        {
            interpolateOne(_preciseGeode.get(), _visiblePrecise, refTime);
            interpolateOne(_starlinkGeode.get(), _allStarlink, refTime);
        }
    protected:
        void interpolateOne(osg::Geode* geode, std::vector<Satellite>& sats, double refTime)
        {
            if (!geode || sats.empty()) return;
            osg::Geometry* g = geode->getDrawable(0)->asGeometry(); if (!g) return;
            osg::Vec3Array* va = static_cast<osg::Vec3Array*>(g->getVertexArray()); if (!va) return;
            for (size_t i = 0; i < sats.size() && i < va->size(); ++i)
            {
                (*va)[i] = earthsat::extrapolateSatelliteEcef(
                    sats[i].ecef, sats[i].ecefVelocity,
                    sats[i].lastUpdateRefTime, refTime);
            }
            va->dirty(); g->dirtyBound();
        }

        void selectByNoradIdInternal(int noradId)
        {
            const Satellite* found = nullptr;
            for (size_t i = 0; i < _visiblePrecise.size(); ++i)
                if (_visiblePrecise[i].noradId == noradId) { found = &_visiblePrecise[i]; break; }
            if (!found) return;
            SatelliteInfo info;
            info.valid = true; info.name = found->name; info.noradId = found->noradId;
            info.category = found->category; info.latDeg = found->latDeg; info.lonDeg = found->lonDeg;
            info.altKm = found->altKm; info.speedKmS = found->speedKmS;
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex);
                _selected = info; _selectedLine1 = found->line1; _selectedLine2 = found->line2;
            }
            _selectedOrbitDirty = true;   // 下一帧(主线程 update)重建该卫星的轨道线+足迹圆
        }

        // 从"现在"起算的 tsince(分钟since该 TLE 自身历元),与 repropagate() 用的是
        // 同一手法——TLE 历元通常是数天前(CelesTrak 数据刷新周期),轨道线必须从
        // "现在"画起,不能从历元(tsince=0)画起,否则画的是"过去某一整圈"而非
        // "未来一整圈"(spec 原话:"画未来一整圈轨道线")。
        static double tsinceNow(const std::string& line1, const std::string& line2)
        {
            try
            {
                libsgp4::Tle tle(line1, line2);
                return (libsgp4::DateTime::Now() - tle.Epoch()).TotalMinutes();
            }
            catch (const std::exception&) { return 0.0; }
        }

        void rebuildOrbitLines()
        {
            _orbitRoot->removeChildren(0, _orbitRoot->getNumChildren());
            SatelliteInfo sel; std::string line1, line2;
            { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex);
              sel = _selected; line1 = _selectedLine1; line2 = _selectedLine2; }
            if (!sel.valid) return;
            double tsince = tsinceNow(line1, line2);
            std::vector<osg::Vec3d> verts = earthsat::buildOrbitVertices(line1, line2, tsince, 180);
            if (verts.size() >= 2)
            {
                osg::Geometry* g = earthgeo::buildPolylineGeometry(verts, osg::Vec4(1.0f, 0.9f, 0.3f, 1.0f));
                osg::Geode* geode = new osg::Geode; geode->addDrawable(g);
                _orbitRoot->addChild(geode);
            }
            double radiusKm = earthsat::footprintRadiusKm(sel.altKm);
            std::vector<osg::Vec3d> ring = earthsat::buildFootprintVertices(sel.latDeg, sel.lonDeg, radiusKm, 64);
            if (ring.size() >= 2)
            {
                osg::Geometry* g = earthgeo::buildPolylineGeometry(ring, osg::Vec4(1.0f, 0.5f, 0.2f, 1.0f));
                osg::Geode* geode = new osg::Geode; geode->addDrawable(g);
                _orbitRoot->addChild(geode);
            }
        }
    public:
        osg::Group* buildScene()
        {
            _root = new osg::Group; _root->setName("SatelliteLayer");
            std::string fsSrc = std::string(earthmark::markerShapeGLSL()) + satFragMain;
            osg::Shader* vs = new osg::Shader(osg::Shader::VERTEX, satVertCode);
            osg::Shader* fs = new osg::Shader(osg::Shader::FRAGMENT, fsSrc);
            vs->setName("Sat_VS"); fs->setName("Sat_FS");
            osgVerse::Pipeline::createShaderDefinitions(vs, 100, 130);
            osgVerse::Pipeline::createShaderDefinitions(fs, 100, 130);
            osg::ref_ptr<osg::Program> prog = new osg::Program;
            prog->addShader(vs); prog->addShader(fs);
            _ss = new osg::StateSet;
            _ss->setAttributeAndModes(prog.get(), osg::StateAttribute::ON);
            _ss->setMode(GL_PROGRAM_POINT_SIZE, osg::StateAttribute::ON);
            _ss->setRenderBinDetails(11, "RenderBin");

            _preciseRoot = new osg::Group; _preciseRoot->setName("SatPrecise");
            _starlinkRoot = new osg::Group; _starlinkRoot->setName("SatStarlink");
            _starlinkRoot->setNodeMask(0);   // 默认关(Starlink 不在"全部"预设里)
            _root->addChild(_preciseRoot.get());
            _root->addChild(_starlinkRoot.get());
            _orbitRoot = new osg::Group; _orbitRoot->setName("SatOrbitLines");
            _root->addChild(_orbitRoot.get());
            _root->addUpdateCallback(new SyncCallback(this));
            return _root;
        }

    protected:
        virtual ~SatelliteLayerImpl()
        { if (_thread) { _thread->cancel(); _thread->join(); delete _thread; _thread = nullptr; } }

        std::string _cacheDir;
        osg::Group* _root;   // 裸指针:由 UserData 拥有,同 flight_data.cpp 约定(无引用环)
        osg::ref_ptr<osg::Group> _preciseRoot, _starlinkRoot, _orbitRoot;
        osg::ref_ptr<osg::Geode> _preciseGeode, _starlinkGeode;
        osg::ref_ptr<osg::StateSet> _ss;
        bool _catStation, _catNav, _catWeather, _catStarlink;
        std::atomic<bool> _preciseFetchTriggered, _starlinkFetchTriggered;   // 主线程 setCategoryEnabled()
                                                   // 写 / 后台 FetchThread::run() 读,跨线程。
        // 主线程写、后台抓取线程读并清零。
        std::atomic<bool> _preciseRefetchRequested{false};
        std::atomic<bool> _starlinkRefetchRequested{false};   // 主线程写(setCategoryEnabled 检测到"上次拉取
                                                   // 空+重新打开开关"时置位)、后台线程读并清零——
                                                   // 方向与 _preciseFetchTriggered 相反但同样是
                                                   // 既有的跨线程 bool 惯例,跨线程 → atomic。
        bool _preciseFetchDone, _starlinkFetchDone;   // 只在主线程 syncIfDirty() 里置位(消费快照
                                                       // 的同一处),避免和 _allPrecise/_allStarlink
                                                       // 的读取出现时序竞态;fetchErrorText() 也只
                                                       // 从主线程调用,两边天然同步,不需要加锁。
        std::vector<Satellite> _allPrecise, _allStarlink, _pendingPrecise, _pendingStarlink, _visiblePrecise;
        std::string _pendingStarlinkErrorDetail, _starlinkErrorDetail;   // 同 _preciseFetchDone 一样,
                                                                          // _starlinkErrorDetail 只在
                                                                          // syncIfDirty() 主线程侧写。
        OpenThreads::Mutex _mutex;
        bool _preciseDirty, _starlinkDirty, _rebuildNeeded;
        SatelliteInfo _selected; std::string _selectedLine1, _selectedLine2;
        mutable OpenThreads::Mutex _selMutex;
        std::atomic<bool> _selectedOrbitDirty{false};
    public:
        FetchThread* _thread;
    };

    void SyncCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        double refTime = nv->getFrameStamp() ? nv->getFrameStamp()->getReferenceTime() : 0.0;
        _owner->syncIfDirty(refTime);
        _owner->interpolate(refTime);
        traverse(node, nv);
    }

    void FetchThread::run()
    {
        bool preciseFetchedOnce = false, starlinkFetchedOnce = false;
        double lastPreciseUpdate = 0.0, lastStarlinkUpdate = 0.0;
        while (!_done)
        {
            // 真机网络抓取真实耗时数秒(fixture 测试是同步读本地文件,几乎零耗时,从未
            // 暴露过这条时序缝隙)——刚抓完的这一轮把 lastPreciseUpdate/lastStarlinkUpdate
            // 设为"此刻",让下面的重新传播判断本轮循环不要立刻触发:否则 currentPrecise()
            // 会读到主线程还没来得及消费的旧快照(仍是空的),repropagate 一个空 vector 后
            // 再 postPreciseSnapshot() 就会把刚抓到的真实卫星列表原地覆盖回空——真机上这是
            // 必现 bug(表现为"开关点了、状态变了,但地球上什么都看不到")。
            // 精选组三类目任一在已完成但缺数据后关闭再打开,同一轮立即放行重新拉取。
            if (_owner->takePreciseRefetchRequest()) preciseFetchedOnce = false;
            if (_owner->preciseFetchTriggered() && !preciseFetchedOnce)
            {
                _owner->postPreciseSnapshot(fetchPreciseSatellites(_owner->cacheDir()));
                preciseFetchedOnce = true;
                lastPreciseUpdate = (double)time(nullptr);
            }
            // 上次拉取空手而归、用户关掉又重新打开开关——放行再拉一次;取走请求(读一次即清零)
            // 放在 !starlinkFetchedOnce 判断之前,这样同一轮循环下面的 if 就能立刻重新触发。
            if (_owner->takeStarlinkRefetchRequest()) starlinkFetchedOnce = false;
            if (_owner->starlinkFetchTriggered() && !starlinkFetchedOnce)
            {
                std::string errDetail;
                std::vector<Satellite> sats = fetchStarlinkSatellites(_owner->cacheDir(), &errDetail);
                _owner->postStarlinkSnapshot(sats, errDetail);
                starlinkFetchedOnce = true;
                lastStarlinkUpdate = (double)time(nullptr);
            }
            double now = (double)time(nullptr);
            if (preciseFetchedOnce && (now - lastPreciseUpdate) >= 1.0)   // 精选组每 1s
            {
                std::vector<Satellite> snapshot = _owner->currentPrecise();
                // 双保险:即使时序窗口之外仍然读到空(比如主线程被极端卡住),也不要用空
                // 快照覆盖已有数据——宁可这一轮跳过重新传播,等下一轮再试。
                if (!snapshot.empty())
                {
                    repropagate(snapshot, lastPreciseUpdate, now);
                    _owner->postPreciseSnapshot(snapshot);
                }
                lastPreciseUpdate = now;
            }
            if (starlinkFetchedOnce && (now - lastStarlinkUpdate) >= 5.0)   // Starlink 每 5s
            {
                std::vector<Satellite> snapshot = _owner->currentStarlink();
                if (!snapshot.empty())
                {
                    repropagate(snapshot, lastStarlinkUpdate, now);
                    _owner->postStarlinkSnapshot(snapshot);
                }
                lastStarlinkUpdate = now;
            }
            OpenThreads::Thread::microSleep(200000);
        }
        _done = true;
    }

    static const float kSatDragThreshPx2 = 25.0f;

    class SatPickHandler : public osgGA::GUIEventHandler
    {
    public:
        SatPickHandler(SatelliteLayerImpl* o) : _owner(o), _downX(0.0f), _downY(0.0f), _pushed(false) {}
        virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
        {
            if (ea.getEventType() == osgGA::GUIEventAdapter::PUSH
                && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            { _downX = ea.getX(); _downY = ea.getY(); _pushed = true; }
            else if (ea.getEventType() == osgGA::GUIEventAdapter::RELEASE
                     && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            {
                float dx = ea.getX() - _downX, dy = ea.getY() - _downY;
                if (_pushed && dx*dx + dy*dy < kSatDragThreshPx2)
                {
                    osgViewer::View* view = static_cast<osgViewer::View*>(&aa);
                    osg::Camera* cam = view->getCamera();
                    const osg::Viewport* vp = cam->getViewport();
                    if (vp)
                    {
                        float my = ea.getY();
                        if (ea.getMouseYOrientation() == osgGA::GUIEventAdapter::Y_INCREASING_DOWNWARDS)
                            my = vp->height() - my;
                        const osg::FrameStamp* frameStamp = view->getFrameStamp();
                        double refTime = frameStamp ? frameStamp->getReferenceTime() : 0.0;
                        _owner->pickAt(cam, ea.getX(), my, refTime);
                    }
                }
                _pushed = false;
            }
            return false;
        }
    protected:
        SatelliteLayerImpl* _owner; float _downX, _downY; bool _pushed;
    };
}

osg::Node* configureSatelliteLayer(osgViewer::View& viewer, osg::Node* /*earthRoot*/,
                                   const std::string& /*mainFolder*/, SatelliteLayer** outLayer)
{
    osg::ref_ptr<SatelliteLayerImpl> impl = new SatelliteLayerImpl();
    osg::Group* root = impl->buildScene();
    root->setUserData(impl.get());
    if (outLayer) *outLayer = impl.get();
    impl->_thread = new FetchThread(impl.get());
    impl->_thread->startThread();
    viewer.addEventHandler(new SatPickHandler(impl.get()));
    return root;
}
