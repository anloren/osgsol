// AIS 船舶层(P3 T5):aisstream.io WebSocket 实时流 → 存量表 → 箭头点精灵。
// 与 flight_data.cpp 的差异:数据不是"整批快照拉取"而是逐条消息推送,因此多一张
// mmsi→AisShip 存量表;WS 连接管理(订阅 bbox / 高空闸门 / 断线退避重连)放在独立
// 的控制线程,libhv 回调线程只记录事实(消息入表、断线置标志),绝不自己做重连
// 决策(61479fc5 / 10a2a8ba 两次跨线程竞态 bug 的教训:决策与消费各归其位)。
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/NodeCallback>
#include <osgGA/GUIEventHandler>
#include <osgViewer/View>
#include <ui/ImGuiComponents.h>   // ImGui::(GetCurrentContext/GetIO),同 feed_layer.cpp
#include <OpenThreads/Thread>
#include <OpenThreads/Mutex>
#include <modeling/Math.h>
#include <pipeline/Pipeline.h>
#include <VerseCommon.h>
#include "earth_config.h"
#include <algorithm>
#include <atomic>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <picojson.h>
#include "3rdparty/libhv/all/client/WebSocketClient.h"
#include "ais_math.h"
#include "ais_data.h"
#include "marker_style.h"

namespace
{
    // 海面标记抬离地表,防 z-fight、保可见(同 feed_layer.cpp kFeedLiftMeters 先例)。
    static const double kShipLiftMeters = 3000.0;
    static const size_t kMaxShips = 5000;          // 存量表容量上限:满了丢新 MMSI,已有的仍更新
    static const double kShipExpireSeconds = 600.0; // >600s 没再收到位置的船从表里剔除
    static const float kShipSizePx = 16.0f;
    static const char* kAisStreamUrl = "wss://stream.aisstream.io/v0/stream";

    // 线程模型(三方职责,红线:done/status 只在消费线程置位,61479fc5 教训):
    // - libhv loop 线程(WebSocketClient 内部):onmessage 里 parseAisMessage → 持 _storeMutex
    //   写 _store(map<mmsi,AisShip>),容量到 5000 时丢弃新 MMSI(已有的仍更新)。
    // - 控制线程(OpenThreads,100ms tick):读 enabled/viewState → 高空闸门/连接/重连/退避
    //   决策(全部 WS open/close 都只在这条线程发起,回调线程绝不自己重连);每 10 tick(1s)
    //   剔除 >600s 未更新的船、持 _storeMutex 拷贝快照 → postSnapshot 给主线程。
    // - 主线程:SyncCallback → syncIfDirty 重建 Geode(箭头点精灵,COG 旋转,SOG 配色)。
    //
    // 状态机(_status,std::atomic<int>,控制线程写、主线程 statusText() 读):
    enum ShipStatus { kNoKey, kDisabled, kZoomedOut, kConnecting, kConnected, kRetrying, kFixture };

    // 渲染快照条目(控制线程从存量表拷出后补 ECEF,主线程只读)。
    struct Ship
    {
        long long mmsi = 0;
        double lat = 0, lon = 0, sogKn = 0, cogDeg = 0, lastSeenUnix = 0;
        std::string name;
        osg::Vec3d ecef;
    };

    // 点精灵:VS 写 gl_PointSize(取自 texcoord0.x)+ headingRad(texcoord0.y)。
    // FS 形状绘制改用 earthmark::markerShapeGLSL() 共享库(marker T3),固定 Ship 五边形
    // (shapeId=2),随 COG(headingRad)旋转。
    // MRT 双输出(COLOR_BUFFER0 颜色 + COLOR_BUFFER1 掩码),逐字复用 flight_data.cpp。
    const char* shipVertCode = {
        "VERSE_VS_OUT vec4 pointColor;\n"
        "VERSE_VS_OUT float headingRad;\n"
        "void main() {\n"
        "    pointColor = osg_Color;\n"
        "    gl_PointSize = osg_MultiTexCoord0.x;\n"
        "    headingRad = osg_MultiTexCoord0.y;\n"
        // 前半球剔除(取代深度测试,消除远视角转动 z-fight 闪烁):见 feed_layer.cpp 同款数学。\n"
        "    vec4 Pv = VERSE_MATRIX_MV * osg_Vertex;\n"
        "    vec3 Cv = (VERSE_MATRIX_MV * vec4(0.0, 0.0, 0.0, 1.0)).xyz;\n"
        "    if (dot(Pv.xyz - Cv, Pv.xyz) >= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }\n"
        "    gl_Position = VERSE_MATRIX_MVP * osg_Vertex;\n"
        "}\n"
    };
    // FS 入口:实际形状绘制逻辑在 earthmark::markerShapeGLSL() 共享库里(buildScene()
    // 拼接在本字符串之前,提供 markerCoverage)——船固定 Ship 五边形(shapeId=2),随 COG 旋转。
    const char* shipFragMain = {
        "VERSE_FS_IN vec4 pointColor;\n"
        "VERSE_FS_IN float headingRad;\n"
        "#ifdef VERSE_GLES3\n"
        "layout(location=0) VERSE_FS_OUT vec4 fragColor;\n"
        "layout(location=1) VERSE_FS_OUT vec4 fragOrigin;\n"
        "#endif\n"
        "void main() {\n"
        "    float cov = markerCoverage(2, gl_PointCoord, headingRad);\n"
        "    if (cov <= 0.0) discard;\n"
        "    vec3 rgb = mix(pointColor.rgb * 0.7, pointColor.rgb, cov);\n"
        "#ifdef VERSE_GLES3\n"
        "    fragColor = vec4(rgb, 1.0); fragOrigin = vec4(1.0);\n"
        "#else\n"
        "    gl_FragData[0] = vec4(rgb, 1.0); gl_FragData[1] = vec4(1.0);\n"
        "#endif\n"
        "}\n"
    };

    // SOG(节)→颜色:锚泊灰 / 慢速蓝 / 常速青 / 高速亮黄。
    osg::Vec4 sogColor(double sogKn)
    {
        if (sogKn < 0.5)  return osg::Vec4(0.62f, 0.62f, 0.62f, 1.0f);   // 锚泊:灰
        if (sogKn < 8.0)  return osg::Vec4(0.35f, 0.65f, 1.00f, 1.0f);   // 慢速:蓝
        if (sogKn < 20.0) return osg::Vec4(0.30f, 0.95f, 0.90f, 1.0f);   // 常速:青
        return osg::Vec4(1.00f, 0.95f, 0.40f, 1.0f);                     // 高速:亮黄
    }

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

    // 由一批 Ship 构建一个 Geode(GL_POINTS),同 buildFlightGeode 手法。
    osg::Geode* buildShipGeode(const std::vector<Ship>& ss, osg::StateSet* sharedSS)
    {
        osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
        osg::ref_ptr<osg::Vec2Array> attrs = new osg::Vec2Array;  // (sizePx, headingRad)
        for (size_t i = 0; i < ss.size(); ++i)
        {
            verts->push_back(ss[i].ecef);
            colors->push_back(sogColor(ss[i].sogKn));
            attrs->push_back(osg::Vec2(kShipSizePx, (float)osg::DegreesToRadians(ss[i].cogDeg)));
        }
        geom->setVertexArray(verts.get());
        geom->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
        geom->setTexCoordArray(0, attrs.get());
        geom->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, (GLsizei)verts->size()));
        geom->setUseDisplayList(false); geom->setUseVertexBufferObjects(true);
        geom->setCullingActive(false);
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(geom.get()); geode->setStateSet(sharedSS);
        return geode.release();
    }

    class ShipLayerImpl;

    class SyncCallback : public osg::NodeCallback
    {
    public:
        SyncCallback(ShipLayerImpl* o) : _owner(o) {}
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);  // 定义在 ShipLayerImpl 之后
    protected:
        ShipLayerImpl* _owner;
    };

    // 控制线程:100ms tick,承担全部 WS 生命周期决策与周期性快照发布。
    class ControlThread : public OpenThreads::Thread
    {
    public:
        ControlThread(ShipLayerImpl* o) : _owner(o), _done(false) {}
        virtual int cancel() { _done = true; return OpenThreads::Thread::cancel(); }
        virtual void run();   // 定义在 ShipLayerImpl 之后(需其完整定义)
    protected:
        ShipLayerImpl* _owner; std::atomic<bool> _done;   // cancel() 主线程写 / run() 控制线程读,跨线程
    };

    class ShipLayerImpl : public osg::Referenced, public ShipLayer
    {
    public:
        ShipLayerImpl() : _root(0), _enabled(false), _dirty(false),
                          _camAltM(1.0e7), _thread(nullptr),
                          _status((int)kNoKey), _shipCount(0),
                          _wsClosed(false), _wsOpened(false), _fixtureMode(false)
        {
            // 环境变量优先,回退磁盘 keys.env(双击 .app 启动读不到环境变量,见 earth_config.h)。
            std::string key = earthcfg::resolveKey("EARTH_AISSTREAM_KEY");
            if (!key.empty()) _apiKey = key;
            const char* fixture = getenv("EARTH_SHIPS_FILE");
            // fixture 优先于 key(离线确定性):设了 EARTH_SHIPS_FILE 就绝不联网。
            if (fixture && *fixture) { _fixtureMode = true; loadFixture(fixture); }
            else if (_apiKey.empty()) _status = (int)kNoKey;
            else _status = (int)kDisabled;
        }

        virtual void setEnabled(bool on)
        {
            _enabled = on;
            if (_root) _root->setNodeMask(on ? ~0u : 0u);
        }
        virtual bool isEnabled() const { return _enabled; }

        virtual void setViewState(double latMin, double lonMin,
                                  double latMax, double lonMax, double camAltM)
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_viewMutex);
            _viewBBox.latMin = latMin; _viewBBox.lonMin = lonMin;
            _viewBBox.latMax = latMax; _viewBBox.lonMax = lonMax;
            _camAltM = camAltM;
        }

        virtual ShipInfo getSelected() const
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex); return _selected; }
        virtual void clearSelected()
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex); _selected = ShipInfo(); }

        // 汇总统计,供 AI 工具调用。_ships 只在主线程写(syncIfDirty,update 遍历),
        // AIChatCore::drainMainThread 也在主线程(FRAME handler)调用本方法,因此直接读
        // _ships 是安全的,不需要加锁(同 flight_data.cpp summaryJson 的既有约定)。
        // 覆盖范围为当前订阅 bbox(aisstream 只推送订阅范围内的船),不是全球总数。
        virtual std::string summaryJson() const
        {
            picojson::object r;
            r["count"] = picojson::value((double)_ships.size());
            r["note"] = picojson::value(std::string(u8"覆盖范围为当前订阅 bbox,非全球总数"));
            int nAnchored = 0, nSlow = 0, nCruise = 0, nFast = 0, fastestIdx = -1;
            for (size_t i = 0; i < _ships.size(); ++i)
            {
                double v = _ships[i].sogKn;
                if (v < 0.5) ++nAnchored;
                else if (v < 8.0) ++nSlow;
                else if (v < 20.0) ++nCruise;
                else ++nFast;
                if (fastestIdx < 0 || v > _ships[fastestIdx].sogKn) fastestIdx = (int)i;
            }
            picojson::object bySpeed;
            bySpeed["anchored"] = picojson::value((double)nAnchored);
            bySpeed["slow"] = picojson::value((double)nSlow);
            bySpeed["cruise"] = picojson::value((double)nCruise);
            bySpeed["fast"] = picojson::value((double)nFast);
            r["bySpeed"] = picojson::value(bySpeed);
            if (fastestIdx >= 0)
            {
                r["fastestName"] = picojson::value(_ships[fastestIdx].name);
                r["fastestSogKn"] = picojson::value(_ships[fastestIdx].sogKn);
            }
            return picojson::value(r).serialize();
        }

        // 主线程读;_status/_shipCount 都是 atomic,控制线程写,不加锁(接口注释约定)。
        virtual std::string statusText() const
        {
            switch ((ShipStatus)_status.load())
            {
            case kNoKey:     return u8"未配置 key(EARTH_AISSTREAM_KEY)";
            case kZoomedOut: return u8"推近后显示船舶(当前视野过大)";
            case kConnecting: return u8"连接中…";
            case kConnected:
            {
                char buf[64];
                snprintf(buf, sizeof(buf), u8"已连接 · %d 艘", _shipCount.load());
                return buf;
            }
            case kRetrying:  return u8"连接失败,自动重试中";
            case kFixture:   return u8"离线 fixture 数据";
            default:         return "";   // kDisabled
            }
        }

        // 屏幕拾取:相机 + 窗口鼠标坐标(y 向上)。选最近的前半球船。仅主线程调用。
        void pickAt(osg::Camera* cam, float mx, float my)
        {
            if (!_enabled || _ships.empty() || !cam->getViewport()) return;
            osg::Vec3d eye, center, up; cam->getViewMatrixAsLookAt(eye, center, up);
            osg::Matrixd VPW = cam->getViewMatrix() * cam->getProjectionMatrix()
                             * cam->getViewport()->computeWindowMatrix();
            double bestD2 = 1e18; int best = -1;
            for (size_t i = 0; i < _ships.size(); ++i)
            {
                const osg::Vec3d& P = _ships[i].ecef;
                if ((eye * P) <= (P * P)) continue;          // 前半球
                osg::Vec3d win = P * VPW;
                double d2 = (win.x()-mx)*(win.x()-mx) + (win.y()-my)*(win.y()-my);
                float tol = 14.0f;
                if (d2 < (double)(tol*tol) && d2 < bestD2) { bestD2 = d2; best = (int)i; }
            }
            if (best >= 0)
            {
                const Ship& s = _ships[best];
                ShipInfo info; info.valid = true;
                info.name = s.name; info.mmsi = s.mmsi;
                info.lat = s.lat; info.lon = s.lon;
                info.sogKn = s.sogKn; info.cogDeg = s.cogDeg;
                info.ageSec = (double)time(nullptr) - s.lastSeenUnix;
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex); _selected = info;
            }
        }

        // ---------- 控制线程侧接口(下面这组只被 ControlThread::run() 调用) ----------

        bool fixtureMode() const { return _fixtureMode; }
        bool hasKey() const { return !_apiKey.empty(); }

        void getViewState(earthais::ShipBBox& bb, double& camAltM)
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_viewMutex);
            bb = _viewBBox; camAltM = _camAltM;
        }

        void setStatus(ShipStatus st) { _status = (int)st; }

        bool wsAlive() const { return _ws.get() != nullptr; }
        bool wsClosedFlag() const { return _wsClosed.load(); }
        bool wsOpenedFlag() const { return _wsOpened.load(); }
        bool needsResubscribe(const earthais::ShipBBox& view) const
        { return earthais::bboxNeedsResubscribe(_subscribedBBox, view); }

        // 建立新连接并订阅(仅控制线程)。返回 false = open() 立即失败(由调用方退避)。
        // 订阅 bbox 外扩 1.5 倍:容忍视口小幅平移而不必每次都断线重订阅。
        bool connectWs(const earthais::ShipBBox& view)
        {
            _subscribedBBox = earthais::inflateBBox(view, 1.5);
            std::vector<earthais::ShipBBox> boxes =
                earthais::splitSubscriptionBoxes(_subscribedBBox);
            std::string sub = earthais::buildSubscriptionJson(_apiKey, boxes);
            _wsClosed = false; _wsOpened = false;
            _ws.reset(new hv::WebSocketClient);
            hv::WebSocketClient* c = _ws.get();   // 回调在 libhv loop 线程执行;client 对象
            ShipLayerImpl* self = this;           // 由控制线程独占管理,析构前先 close(见
                                                  // ensureDisconnected),回调不会悬空。
            // libhv 回调线程只记录事实:发订阅、置 opened/closed 标志、消息入表。
            // 绝不在回调里做 open/close/重连决策(红线)。
            // 连接成功打一行日志(对齐 [Feed]/[Sat] 惯例):真机验证(2026-07-06)时网络
            // 模式全程零 stdout,只能靠截图盲验——留个抓手给未来诊断/E2E 日志断言。
            _ws->onopen = [c, sub, self]() {
                c->send(sub); self->_wsOpened = true;
                std::cout << "[Ship] ws connected, subscription sent\n";
            };
            _ws->onclose = [self]() { self->_wsClosed = true; };
            _ws->onmessage = [self](const std::string& msg) { self->onWsMessage(msg); };
            _ws->setPingInterval(25000);
            if (_ws->open(kAisStreamUrl) != 0) { ensureDisconnected(); return false; }
            return true;
        }

        // 断开并销毁 client(仅控制线程)。TcpClientTmpl 析构 = closesocket + 停掉并 join
        // 内部 loop 线程,reset() 返回后回调保证不再触发——client 对象一定活过任何回调。
        void ensureDisconnected()
        {
            if (!_ws) return;
            _ws->close();
            _ws.reset();
            _wsClosed = false; _wsOpened = false;
        }

        // 每 1s(10 tick)一次:剔除 >600s 未更新的船,再把存量表拷成渲染快照交给主线程。
        // ECEF 换算放在锁外(纯数学,最多 5000 次三角函数,不占着 _storeMutex 拖慢
        // libhv 回调线程的消息入表)。
        void expireAndSnapshot()
        {
            double now = (double)time(nullptr);
            std::vector<earthais::AisShip> raw;
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_storeMutex);
                for (std::map<long long, earthais::AisShip>::iterator it = _store.begin();
                     it != _store.end();)
                {
                    if (now - it->second.lastSeenUnix > kShipExpireSeconds) _store.erase(it++);
                    else { raw.push_back(it->second); ++it; }
                }
            }
            std::vector<Ship> snap; snap.reserve(raw.size());
            for (size_t i = 0; i < raw.size(); ++i)
            {
                Ship v; v.mmsi = raw[i].mmsi; v.name = raw[i].name;
                v.lat = raw[i].lat; v.lon = raw[i].lon;
                v.sogKn = raw[i].sogKn; v.cogDeg = raw[i].cogDeg;
                v.lastSeenUnix = raw[i].lastSeenUnix;
                v.ecef = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
                    osg::DegreesToRadians(v.lat), osg::DegreesToRadians(v.lon), kShipLiftMeters));
                snap.push_back(v);
            }
            _shipCount = (int)snap.size();   // kConnected 文案的 N(atomic,主线程读)
            postSnapshot(snap);
        }

        // ---------- libhv 回调线程侧 ----------

        // onmessage(libhv loop 线程):解析 → 持 _storeMutex 入表。仅此而已。
        void onWsMessage(const std::string& msg)
        {
            earthais::AisShip s = earthais::parseAisMessage(msg);
            if (!s.valid) return;
            s.lastSeenUnix = (double)time(nullptr);
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_storeMutex);
            std::map<long long, earthais::AisShip>::iterator it = _store.find(s.mmsi);
            if (it != _store.end())
            {
                if (s.name.empty()) s.name = it->second.name;   // 新报文没带船名就保留旧的
                it->second = s;
            }
            else if (_store.size() < kMaxShips) _store[s.mmsi] = s;
            // 容量到顶:丢弃新 MMSI(已有的仍更新)——防极端订阅范围下无界增长。
        }

        // ---------- 主线程侧 ----------

        // 控制线程交付新快照(加锁)。
        void postSnapshot(const std::vector<Ship>& ss)
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_snapMutex); _pending = ss; _dirty = true; }

        // 主线程(update 遍历)调用:若有新数据则重建 geode。
        // 刻意不做逐帧外推(与航班层的差异):船速 ~10m/s,快照 1s 一发,帧间位移
        // 远小于 1px,外推纯属浪费;航班 ~250m/s 才需要。
        void syncIfDirty()
        {
            std::vector<Ship> ss;
            { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_snapMutex);
              if (!_dirty) return; ss = _pending; _dirty = false; }
            _ships = ss;
            if (_geode.valid()) _root->removeChild(_geode.get());
            _geode = buildShipGeode(ss, _ss.get());
            _root->addChild(_geode.get());
        }

        void startControl() { if (!_thread) { _thread = new ControlThread(this); _thread->startThread(); } }

        osg::Group* buildScene()
        {
            _root = new osg::Group;
            _root->setName("ShipLayer");
            _root->setNodeMask(0);   // 默认关

            // StateSet/Program 逐实例各自持有(本仓库既有惯例,不共享全局 Program——
            // P1 踩过全局 static 共享 Program 导致瓦片染色的坑)。
            std::string fsSrc = std::string(earthmark::markerShapeGLSL()) + shipFragMain;
            osg::Shader* vs = new osg::Shader(osg::Shader::VERTEX, shipVertCode);
            osg::Shader* fs = new osg::Shader(osg::Shader::FRAGMENT, fsSrc);
            vs->setName("Ship_VS"); fs->setName("Ship_FS");
            osgVerse::Pipeline::createShaderDefinitions(vs, 100, 130);
            osgVerse::Pipeline::createShaderDefinitions(fs, 100, 130);
            osg::ref_ptr<osg::Program> prog = new osg::Program;
            prog->addShader(vs); prog->addShader(fs);
            _ss = new osg::StateSet;
            _ss->setAttributeAndModes(prog.get(), osg::StateAttribute::ON);
            _ss->setMode(GL_PROGRAM_POINT_SIZE, osg::StateAttribute::ON);
            // 关深度测试:可见性由 VS 前半球剔除决定,根治远视角转动 z-fight 闪烁(见上方 VS)。
            _ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
            _ss->setRenderBinDetails(11, "RenderBin");  // 在地表/海洋 pass 之后画

            _root->addUpdateCallback(new SyncCallback(this));
            return _root;
        }

    protected:
        virtual ~ShipLayerImpl()
        {
            // cancel 置 done → join;控制线程 run() 退出前自己 ensureDisconnected()
            // 关掉 WS(WS 生命周期从建到拆都锁定在控制线程,主线程不碰)。
            if (_thread) { _thread->cancel(); _thread->join(); delete _thread; _thread = nullptr; }
            // 双保险:即使未来有人改回可取消线程,也不让 WS 拆除逃逸到成员析构——
            // 此时控制线程已死(join 返回),_wsClosed/_wsOpened 仍存活,调用安全;
            // ensureDisconnected() 本身是幂等的(_ws 已空则直接返回)。
            ensureDisconnected();
        }

        // fixture 模式(configure 时主线程一次性执行,此时控制线程尚未启动,无并发):
        // 逐行读 JSONL 过 parseAisMessage 灌 _store,post 一次快照,不建 WS。
        void loadFixture(const char* path)
        {
            std::ifstream in(path);
            if (!in) { std::cout << "[Ship] fixture open failed: " << path << "\n"; _status = (int)kFixture; return; }
            double now = (double)time(nullptr);
            std::string line;
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_storeMutex);
                while (std::getline(in, line))
                {
                    if (line.empty()) continue;
                    earthais::AisShip s = earthais::parseAisMessage(line);
                    if (!s.valid) continue;
                    s.lastSeenUnix = now;
                    if (_store.size() < kMaxShips || _store.count(s.mmsi)) _store[s.mmsi] = s;
                }
            }
            _status = (int)kFixture;
            expireAndSnapshot();   // 刚灌进去的都是"新鲜"的,只为拷快照
            std::cout << "[Ship] fixture loaded " << _shipCount.load() << " ships\n";
        }

        osg::Group* _root;   // 裸指针:由返回节点经 UserData 拥有,本对象不拥有(无引用环)
        osg::ref_ptr<osg::Geode> _geode;
        osg::ref_ptr<osg::StateSet> _ss;

        std::string _apiKey;                       // 构造时读 env,之后只读
        std::vector<Ship> _ships;                  // 主线程副本(syncIfDirty 后)
        std::vector<Ship> _pending; bool _dirty;   // 控制线程写 / 主线程消费
        OpenThreads::Mutex _snapMutex;             // 守护 _pending/_dirty

        std::map<long long, earthais::AisShip> _store;   // libhv 线程写入 / 控制线程剔除+拷贝
        OpenThreads::Mutex _storeMutex;                  // 守护 _store

        earthais::ShipBBox _viewBBox; double _camAltM;   // 主线程写 / 控制线程读
        OpenThreads::Mutex _viewMutex;                   // 守护 _viewBBox/_camAltM

        earthais::ShipBBox _subscribedBBox;   // 仅控制线程读写(connectWs/needsResubscribe),无锁

        // WS client:创建/open/close/销毁全部只在控制线程(fixture 模式下从不创建)。
        std::unique_ptr<hv::WebSocketClient> _ws;

        std::atomic<bool> _enabled;           // 主线程写 / 控制线程读,跨线程 → atomic
        ControlThread* _thread;
        std::atomic<int> _status;             // 控制线程写(fixture/noKey 例外:构造时主线程写一次)
        std::atomic<int> _shipCount;          // 控制线程写 / 主线程读(kConnected 文案的 N)
        std::atomic<bool> _wsClosed;          // libhv 回调线程置位 / 控制线程消费并清零
        std::atomic<bool> _wsOpened;          // libhv 回调线程置位 / 控制线程读
        bool _fixtureMode;
        ShipInfo _selected;
        mutable OpenThreads::Mutex _selMutex;
    };

    void SyncCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        _owner->syncIfDirty();
        traverse(node, nv);
    }

    // 控制线程主循环:退避/重连计时全部是本函数局部变量(天然单线程,零共享)。
    void ControlThread::run()
    {
        // 禁用 pthread 取消:_thread->cancel() 会在 microSleep(100000) 的取消点
        // 直接杀死本线程,跳过 do-while 之后的尾部 ensureDisconnected(),导致带活
        // 回调线程的 hv::WebSocketClient 逃逸到主线程侧成员析构(_wsClosed/_wsOpened
        // 声明在 _ws 之后,先于 _ws 析构,libhv loop 线程 join 期间的 onclose 回调可能
        // 写已析构的 atomic——形式未定义行为)。禁用后 cancel() 只置 _done,线程只能
        // 从 while 条件正常退出,尾部 ensureDisconnected() 保证总是在本线程上跑完。
        setCancelModeDisable();
        double backoffSec = 5.0;          // 失败退避:5s 起步,×2 递增,封顶 60s,成功复位
        double nextRetryTime = 0.0;       // epoch 秒;now >= 此值才允许再次连接
        double lastConnectTime = -1.0e9;  // 距上次连接 >10s 才允许换订阅重连(防抖)
        int snapTick = 0;
        while (!_done)
        {
            do
            {
                if (_owner->fixtureMode()) break;   // 恒 kFixture(构造时已置),无网络
                if (!_owner->hasKey()) { _owner->setStatus(kNoKey); break; }   // 不联网
                if (!_owner->isEnabled())
                { _owner->ensureDisconnected(); _owner->setStatus(kDisabled); break; }

                double now = (double)time(nullptr);
                // 每 10 tick(1s):剔除过期船 + 发布渲染快照(放在闸门之前——即使
                // 视野过大断了连接,存量表里的旧船也要随时间正常过期消失)。
                if (++snapTick >= 10) { snapTick = 0; _owner->expireAndSnapshot(); }

                earthais::ShipBBox view; double camAltM = 0.0;
                _owner->getViewState(view, camAltM);
                // 高空闸门:只在"接近整个地球"的极远视角才断开。阈值 2026-07-06 大幅放宽——
                // 实测 AISStream 免费层数据量温和(区域几 msg/s、准全球才 ~84-300 msg/s、无硬配额),
                // 原来 3000km/40° 的保守闸门是基于"全球消息爆炸"的错误假设。放宽后洲际/半球视角
                // 都能看船流;保留极远断开的意义不再是"扛不住消息",而是省长期挂机带宽(全球 ~0.5GB/h)
                // + 避开存量表 5000 上限下把几万船采样成 5000 的无意义显示。
                if (camAltM > 1.5e7 || (view.lonMax - view.lonMin) > 150.0)
                { _owner->ensureDisconnected(); _owner->setStatus(kZoomedOut); break; }

                if (_owner->wsAlive())
                {
                    if (_owner->wsClosedFlag())
                    {
                        // 回调线程只置了标志,断开/退避/重试全在这里决策(红线)。
                        _owner->ensureDisconnected();
                        nextRetryTime = now + backoffSec;
                        backoffSec = std::min(backoffSec * 2.0, 60.0);
                        _owner->setStatus(kRetrying);
                    }
                    else if (_owner->wsOpenedFlag())
                    {
                        backoffSec = 5.0;   // 连接成功,退避复位
                        _owner->setStatus(kConnected);
                        // 视口移出/远超/深入订阅范围 → 换订阅(close+重开);>10s 防抖,
                        // 避免用户连续平移时疯狂重连。
                        if (_owner->needsResubscribe(view) && (now - lastConnectTime) > 10.0)
                        {
                            _owner->ensureDisconnected();
                            lastConnectTime = now;
                            if (_owner->connectWs(view)) _owner->setStatus(kConnecting);
                            else
                            {
                                nextRetryTime = now + backoffSec;
                                backoffSec = std::min(backoffSec * 2.0, 60.0);
                                _owner->setStatus(kRetrying);
                            }
                        }
                    }
                    else _owner->setStatus(kConnecting);   // 握手进行中
                }
                // 闸门边界振荡防抖:任何新连接都至少距上次连接 10s,与换订阅路径同一
                // 冷却(相机高度在 3.0e6 门限附近抖动会导致闸门反复开合,每次开都立即
                // 重连就是一次握手风暴);退避(nextRetryTime)语义不变,这里只是再加一
                // 道地板,不影响失败重试的退避节奏。
                else if (now >= nextRetryTime && (now - lastConnectTime) > 10.0)
                {
                    lastConnectTime = now;
                    if (_owner->connectWs(view)) _owner->setStatus(kConnecting);
                    else
                    {
                        nextRetryTime = now + backoffSec;
                        backoffSec = std::min(backoffSec * 2.0, 60.0);
                        _owner->setStatus(kRetrying);
                    }
                }
                // now < nextRetryTime 才是"真失败退避"；否则是纯 10s 防抖冷却(从未失败过,
                // 例如刚从 kZoomedOut 恢复),此时报"连接失败重试中"会误导用户——改用
                // kConnecting,与"握手进行中"的措辞语义一致。
                else _owner->setStatus(now < nextRetryTime ? kRetrying : kConnecting);
            } while (false);
            OpenThreads::Thread::microSleep(100000);  // 100ms
        }
        _owner->ensureDisconnected();   // 线程退出前关 WS(析构路径唯一出口)
        _done = true;
    }

    static const float kShipDragThreshPx2 = 25.0f;   // 5px 拖动阈值的平方

    // 点击拾取处理器:镜像 FlightPickHandler + FeedPickHandler 的 ImGui 捕获守卫
    // (鼠标落在 ImGui 面板上时不拾取,防止点面板按钮穿透选中下方的船)。
    class ShipPickHandler : public osgGA::GUIEventHandler
    {
    public:
        ShipPickHandler(ShipLayerImpl* o) : _owner(o), _downX(0.0f), _downY(0.0f), _pushed(false) {}
        virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
        {
            if (ea.getEventType() == osgGA::GUIEventAdapter::PUSH
                && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            { _downX = ea.getX(); _downY = ea.getY(); _pushed = true; }
            else if (ea.getEventType() == osgGA::GUIEventAdapter::RELEASE
                     && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            {
                float dx = ea.getX() - _downX, dy = ea.getY() - _downY;
                bool overImGui = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
                if (_pushed && dx * dx + dy * dy < kShipDragThreshPx2 && !overImGui)   // 点击(非拖动)
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
        ShipLayerImpl* _owner; float _downX, _downY; bool _pushed;
    };
}

osg::Node* configureShipLayer(osgViewer::View& viewer, ShipLayer** outLayer)
{
    osg::ref_ptr<ShipLayerImpl> impl = new ShipLayerImpl;
    osg::Group* root = impl->buildScene();
    root->setUserData(impl.get());
    if (outLayer) *outLayer = impl.get();
    impl->startControl();   // 线程常驻;fixture/noKey/未开启时 tick 内直接短路
    viewer.addEventHandler(new ShipPickHandler(impl.get()));
    // 注意:不在这里挂 bbox/FRAME handler——Task 6 在 earth_main 挂(要喂 camAlt)。
    return root;
}
