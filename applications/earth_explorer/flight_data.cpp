#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Point>
#include <osg/NodeCallback>
#include <osgDB/FileUtils>
#include <osgGA/GUIEventHandler>
#include <osgViewer/View>
#include <OpenThreads/Thread>
#include <OpenThreads/Mutex>
#include <atomic>
#include <modeling/Math.h>
#include <pipeline/Pipeline.h>
#include <pipeline/Utilities.h>
#include <readerwriter/EarthManipulator.h>
#include <VerseCommon.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <picojson.h>
#include "3rdparty/libhv/all/client/requests.h"
#include "marker_style.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>
#include "flight_data.h"
#include "flight_math.h"
#include "geo_bbox.h"

namespace
{
    static const double kFlightLiftMeters = 0.0;

    std::vector<earthflight::FlightTrack> parseOpenSky(const std::string& text)
    {
        std::vector<earthflight::FlightTrack> out;
        picojson::value root; std::string err = picojson::parse(root, text);
        if (!err.empty() || !root.is<picojson::object>()) { std::cout << "[Flight] JSON err: " << err << "\n"; return out; }
        const picojson::value& states = root.get("states");
        if (!states.is<picojson::array>()) return out;
        const picojson::array& arr = states.get<picojson::array>();
        for (size_t i = 0; i < arr.size(); ++i)
        {
            if (!arr[i].is<picojson::array>()) continue;
            const picojson::array& s = arr[i].get<picojson::array>();
            if (s.size() < 11) continue;
            if (!s[0].is<std::string>()) continue;
            const std::string& icao24 = s[0].get<std::string>();
            if (icao24.empty()) continue;
            if (s[8].is<bool>() && s[8].get<bool>()) continue;          // on_ground
            if (!s[5].is<double>() || !s[6].is<double>()) continue;     // 缺经纬度
            earthflight::FlightTrack f;
            f.icao24 = icao24;
            f.lon = s[5].get<double>(); f.lat = s[6].get<double>();
            f.altM = s[7].is<double>() ? s[7].get<double>() : 0.0;
            f.velMS = s[9].is<double>() ? s[9].get<double>() : 0.0;
            f.headingRad = osg::DegreesToRadians(s[10].is<double>() ? s[10].get<double>() : 0.0);
            f.callsign = s[1].is<std::string>() ? s[1].get<std::string>() : "";
            f.country = s[2].is<std::string>() ? s[2].get<std::string>() : "";
            f.ecef = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
                osg::DegreesToRadians(f.lat), osg::DegreesToRadians(f.lon), f.altM + kFlightLiftMeters));
            out.push_back(f);
        }
        return out;
    }

    std::vector<earthflight::FlightTrack> fetchFlights(
        double latMin, double lonMin, double latMax, double lonMax)
    {
        const char* fixtureFile = getenv("EARTH_FLIGHTS_FILE");
        if (fixtureFile && *fixtureFile)
        {
            std::ifstream in(fixtureFile);
            if (!in) { std::cout << "[Flight] fixture open failed\n";
                return std::vector<earthflight::FlightTrack>(); }
            std::stringstream ss; ss << in.rdbuf();
            std::vector<earthflight::FlightTrack> fs = parseOpenSky(ss.str());
            std::cout << "[Flight] Parsed " << fs.size() << " flights (fixture)\n";
            return fs;
        }

        earthgeo::GeoBBox bbox;
        bbox.latMin = latMin; bbox.lonMin = lonMin;
        bbox.latMax = latMax; bbox.lonMax = lonMax;
        std::vector<earthgeo::GeoBBox> queryBoxes =
            earthgeo::splitAntimeridianBBox(bbox);
        std::vector<earthflight::FlightTrack> merged;
        for (size_t i = 0; i < queryBoxes.size(); ++i)
        {
            const earthgeo::GeoBBox& queryBox = queryBoxes[i];
            char url[256];
            snprintf(url, sizeof(url),
                "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
                queryBox.latMin, queryBox.lonMin, queryBox.latMax, queryBox.lonMax);
            requests::Request req(new HttpRequest);
            req->method = HTTP_GET; req->url = url; req->timeout = 20;
            requests::Response resp = requests::request(req);
            if (!resp || resp->status_code != 200)
            {
                std::cout << "[Flight] fetch failed status="
                          << (resp ? (int)resp->status_code : -1) << "\n";
                continue;
            }
            std::vector<earthflight::FlightTrack> fetched = parseOpenSky(resp->body);
            merged = earthflight::mergeFlightsByIcao24(merged, fetched);
        }
        std::cout << "[Flight] Parsed " << merged.size() << " flights (network)\n";
        return merged;
    }

    // 点精灵:VS 写 gl_PointSize(取自 texcoord0.x)+ headingRad(texcoord0.y)。
    // FS 形状绘制改用 earthmark::markerShapeGLSL() 共享库(marker T3),固定 Arrow(shapeId=1)。
    // MRT 双输出(COLOR_BUFFER0 颜色 + COLOR_BUFFER1 掩码),同 quake_data。
    const char* flightVertCode = {
        "VERSE_VS_OUT vec4 pointColor;\n"
        "VERSE_VS_OUT float headingRad;\n"
        "void main() {\n"
        "    pointColor = osg_Color;\n"
        "    gl_PointSize = osg_MultiTexCoord0.x;\n"
        "    headingRad = osg_MultiTexCoord0.y;\n"
        // 前半球剔除(取代深度测试,消除远视角转动时的 z-fight 闪烁):见 feed_layer.cpp\n"
        // feedVertCode 同款数学(相机空间 dot(Pv-Cv,Pv)<0 = 可见)。配合 StateSet 关深度测试。\n"
        "    vec4 Pv = VERSE_MATRIX_MV * osg_Vertex;\n"
        "    vec3 Cv = (VERSE_MATRIX_MV * vec4(0.0, 0.0, 0.0, 1.0)).xyz;\n"
        "    if (dot(Pv.xyz - Cv, Pv.xyz) >= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }\n"
        "    gl_Position = VERSE_MATRIX_MVP * osg_Vertex;\n"
        "}\n"
    };
    // FS 入口:实际形状绘制逻辑在 earthmark::markerShapeGLSL() 共享库里(buildScene()
    // 拼接在本字符串之前,提供 markerCoverage)——航班固定 Arrow(shapeId=1),随 headingRad 旋转。
    const char* flightFragMain = {
        "VERSE_FS_IN vec4 pointColor;\n"
        "VERSE_FS_IN float headingRad;\n"
        "#ifdef VERSE_GLES3\n"
        "layout(location=0) VERSE_FS_OUT vec4 fragColor;\n"
        "layout(location=1) VERSE_FS_OUT vec4 fragOrigin;\n"
        "#endif\n"
        "void main() {\n"
        "    float cov = markerCoverage(1, gl_PointCoord, headingRad);\n"
        "    if (cov <= 0.0) discard;\n"
        "    vec3 rgb = mix(pointColor.rgb * 0.7, pointColor.rgb, cov);\n"
        "#ifdef VERSE_GLES3\n"
        "    fragColor = vec4(rgb, 1.0); fragOrigin = vec4(1.0);\n"
        "#else\n"
        "    gl_FragData[0] = vec4(rgb, 1.0); gl_FragData[1] = vec4(1.0);\n"
        "#endif\n"
        "}\n"
    };

    // 高度→颜色:低暖橙 / 中黄白 / 高冷青蓝。
    osg::Vec4 altColor(double altM)
    {
        if (altM < 3000.0)  return osg::Vec4(1.00f, 0.55f, 0.20f, 1.0f);   // 低:暖橙
        if (altM < 9000.0)  return osg::Vec4(1.00f, 0.95f, 0.70f, 1.0f);   // 中:黄白
        return osg::Vec4(0.45f, 0.85f, 1.00f, 1.0f);                       // 高:冷青蓝
    }
    static const float kFlightSizePx = 22.0f;

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

    // 由一批 FlightTrack 构建一个 Geode(GL_POINTS)。
    osg::Geode* buildFlightGeode(const std::vector<earthflight::FlightTrack>& fs,
                                 osg::StateSet* sharedSS)
    {
        osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
        osg::ref_ptr<osg::Vec2Array> attrs = new osg::Vec2Array;  // (sizePx, headingRad)
        for (size_t i = 0; i < fs.size(); ++i)
        {
            verts->push_back(fs[i].ecef);
            colors->push_back(altColor(fs[i].altM));
            attrs->push_back(osg::Vec2(kFlightSizePx, (float)fs[i].headingRad));
        }
        geom->setVertexArray(verts.get());
        geom->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
        geom->setTexCoordArray(0, attrs.get());
        geom->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, (GLsizei)verts->size()));
        geom->setUseDisplayList(false); geom->setUseVertexBufferObjects(true);
        geom->setCullingActive(false);
        // interpolate() 每 UPDATE 帧改写 verts 并 dirty();多线程绘制(DrawThreadPerContext)下
        // draw 与下一帧 update 之间没有隐式屏障，需显式标 DYNAMIC 让 OSG 加锁同步，否则数据竞争+撕裂。
        // colors/attrs 只在 buildFlightGeode 构建时写一次、之后不再改，故不需要标。
        geom->setDataVariance(osg::Object::DYNAMIC);
        verts->setDataVariance(osg::Object::DYNAMIC);
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(geom.get()); geode->setStateSet(sharedSS);
        return geode.release();
    }

    // 前向声明
    class FlightLayerImpl;

    class SyncCallback : public osg::NodeCallback
    {
    public:
        SyncCallback(FlightLayerImpl* o) : _owner(o) {}
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);  // 仅声明,定义在 FlightLayerImpl 之后
    protected:
        FlightLayerImpl* _owner;
    };

    // 抓取线程:仅图层开启时每 ~12s 拉一次(分片 sleep 便于秒退)。不碰 GL/场景图。
    class FetchThread : public OpenThreads::Thread
    {
    public:
        FetchThread(FlightLayerImpl* o) : _owner(o), _done(false) {}
        virtual int cancel() { _done = true; return OpenThreads::Thread::cancel(); }
        virtual void run();   // 定义在 FlightLayerImpl 之后(需其完整定义)
    protected:
        FlightLayerImpl* _owner; std::atomic<bool> _done;   // cancel() 主线程写 / run() 抓取线程读,跨线程
    };

    class FlightLayerImpl : public osg::Referenced, public FlightLayer
    {
    public:
        FlightLayerImpl() : _root(0), _enabled(false), _dirty(false),
                            _bbLatMin(-85.0), _bbLonMin(-180.0), _bbLatMax(85.0), _bbLonMax(180.0),
                            _thread(nullptr), _refreshNow(false), _t0(0.0) {}
        virtual void setEnabled(bool on)
        {
            _enabled = on;
            if (_root) _root->setNodeMask(on ? ~0u : 0u);
            if (on) _refreshNow = true;
        }
        virtual bool isEnabled() const { return _enabled; }
        virtual void setViewBBox(double latMin, double lonMin, double latMax, double lonMax)
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_bbMutex);
            _bbLatMin = latMin; _bbLonMin = lonMin; _bbLatMax = latMax; _bbLonMax = lonMax;
        }
        virtual FlightInfo getSelected() const
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex); return _selected; }
        virtual void clearSelected()
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex); _selected = FlightInfo(); }

        // 屏幕拾取:相机 + 窗口鼠标坐标(y 向上)。选最近的前半球航班。仅主线程调用。
        void pickAt(osg::Camera* cam, float mx, float my, double refTime)
        {
            if (!_enabled || _flights.empty() || !cam->getViewport()) return;
            double elapsed = std::max(0.0, refTime - _t0);
            osg::Vec3d eye, center, up; cam->getViewMatrixAsLookAt(eye, center, up);
            osg::Matrixd VPW = cam->getViewMatrix() * cam->getProjectionMatrix()
                             * cam->getViewport()->computeWindowMatrix();
            double bestD2 = 1e18; int best = -1;
            earthflight::FlightPosition bestPosition;
            for (size_t i = 0; i < _flights.size(); ++i)
            {
                const earthflight::FlightTrack& flight = _flights[i];
                earthflight::FlightPosition position =
                    earthflight::extrapolateFlightPosition(flight, elapsed);
                const osg::Vec3d& P = position.ecef;
                if ((eye * P) <= (P * P)) continue;          // 前半球
                osg::Vec3d win = P * VPW;
                double d2 = (win.x()-mx)*(win.x()-mx) + (win.y()-my)*(win.y()-my);
                float tol = 14.0f;
                if (d2 < (double)(tol*tol) && d2 < bestD2)
                { bestD2 = d2; best = (int)i; bestPosition = position; }
            }
            if (best >= 0)
            {
                const earthflight::FlightTrack& flight = _flights[best];
                FlightInfo info; info.valid = true;
                info.callsign = flight.callsign; info.country = flight.country;
                info.lon = bestPosition.lon; info.lat = bestPosition.lat;
                info.altM = flight.altM; info.velMS = flight.velMS;
                info.headingDeg = osg::RadiansToDegrees(flight.headingRad);
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex); _selected = info;
            }
        }

        void startFetch() { if (!_thread) { _thread = new FetchThread(this); _thread->startThread(); } }

        // 后台线程读 bbox 的辅助(加锁)
        void getBBox(double& a, double& b, double& c, double& d)
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_bbMutex);
            a = _bbLatMin; b = _bbLonMin; c = _bbLatMax; d = _bbLonMax;
        }

        // 后台线程轮询:是否需要立刻刷新(取走后清零)
        bool takeRefreshNow() { return _refreshNow.exchange(false); }

        osg::Group* root() { return _root; }

        // 汇总统计,供 AI 工具 get_flights_summary 调用。_flights 只在主线程写(syncIfDirty,
        // update 遍历),AIChatCore::drainMainThread 也在主线程(FRAME handler)调用本方法,
        // 因此这里直接读 _flights 是安全的,不需要额外加锁(同 quake_data.cpp summaryJson 的约定)。
        // 覆盖范围随当前视口变化(OpenSky bbox 查询),不是全球航班总数;图层未开启或数据尚未
        // 到达时 _flights 为空,返回 count=0(不是错误)。
        virtual std::string summaryJson() const
        {
            picojson::object r;
            r["count"] = picojson::value((double)_flights.size());
            r["note"] = picojson::value(std::string(u8"覆盖范围为当前视口(OpenSky bbox 查询),非全球总数"));
            if (_flights.empty())
            {
                picojson::object hist;
                hist["<2km"] = picojson::value(0.0); hist["2-8km"] = picojson::value(0.0);
                hist[">=8km"] = picojson::value(0.0);
                r["altHistogram"] = picojson::value(hist);
                return picojson::value(r).serialize();
            }

            int nLow = 0, nMid = 0, nHigh = 0, fastestIdx = 0;
            for (size_t i = 0; i < _flights.size(); ++i)
            {
                double altKm = _flights[i].altM / 1000.0;
                if (altKm < 2.0) ++nLow;
                else if (altKm < 8.0) ++nMid;
                else ++nHigh;
                if (_flights[i].velMS > _flights[fastestIdx].velMS) fastestIdx = (int)i;
            }
            picojson::object hist;
            hist["<2km"] = picojson::value((double)nLow); hist["2-8km"] = picojson::value((double)nMid);
            hist[">=8km"] = picojson::value((double)nHigh);
            r["altHistogram"] = picojson::value(hist);
            const earthflight::FlightTrack& fastest = _flights[fastestIdx];
            r["fastestCallsign"] = picojson::value(fastest.callsign);
            r["fastestSpeedMS"] = picojson::value(fastest.velMS);
            return picojson::value(r).serialize();
        }

        // 后台线程交付新快照(加锁)。
        void postSnapshot(const std::vector<earthflight::FlightTrack>& fs)
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); _pending = fs; _dirty = true; }

        // 主线程(update 遍历)调用:若有新数据则重建 geode，并重置插值基准时刻。
        void syncIfDirty(double refTime)
        {
            std::vector<earthflight::FlightTrack> fs;
            { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
              if (!_dirty) return; fs = _pending; _dirty = false; }
            _t0 = refTime;
            _flights = fs;
            if (_geode.valid()) _root->removeChild(_geode.get());
            _geode = buildFlightGeode(fs, _ss.get());
            _root->addChild(_geode.get());
        }

        // 主线程每帧调用:按速度+航向对顶点位置做线性外推(local-plane 近似)。
        void interpolate(double refTime)
        {
            if (!_geode.valid() || _flights.empty()) return;
            double elapsed = std::max(0.0, refTime - _t0);
            osg::Geometry* g = _geode->getDrawable(0)->asGeometry(); if (!g) return;
            osg::Vec3Array* va = static_cast<osg::Vec3Array*>(g->getVertexArray()); if (!va) return;
            for (size_t i = 0; i < _flights.size() && i < va->size(); ++i)
            {
                const earthflight::FlightTrack& flight = _flights[i];
                earthflight::FlightPosition position =
                    earthflight::extrapolateFlightPosition(flight, elapsed);
                (*va)[i] = position.ecef;
            }
            va->dirty(); g->dirtyBound();
        }

        osg::Group* buildScene()
        {
            _root = new osg::Group;
            _root->setNodeMask(0);   // 默认关

            std::string fsSrc = std::string(earthmark::markerShapeGLSL()) + flightFragMain;
            osg::Shader* vs = new osg::Shader(osg::Shader::VERTEX, flightVertCode);
            osg::Shader* fs = new osg::Shader(osg::Shader::FRAGMENT, fsSrc);
            vs->setName("Flight_VS"); fs->setName("Flight_FS");
            osgVerse::Pipeline::createShaderDefinitions(vs, 100, 130);
            osgVerse::Pipeline::createShaderDefinitions(fs, 100, 130);
            osg::ref_ptr<osg::Program> prog = new osg::Program;
            prog->addShader(vs); prog->addShader(fs);
            _ss = new osg::StateSet;
            _ss->setAttributeAndModes(prog.get(), osg::StateAttribute::ON);
            _ss->setMode(GL_PROGRAM_POINT_SIZE, osg::StateAttribute::ON);
            // 关深度测试:可见性由 VS 前半球剔除决定,根治远视角转动 z-fight 闪烁(见 flightVertCode)。
            _ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
            _ss->setRenderBinDetails(11, "RenderBin");  // 在地表/海洋 pass 之后画

            _root->addUpdateCallback(new SyncCallback(this));
            return _root;
        }
    protected:
        virtual ~FlightLayerImpl()
        { if (_thread) { _thread->cancel(); _thread->join(); delete _thread; _thread = nullptr; } }

        osg::Group* _root;   // 裸指针:由返回节点经 UserData 拥有,本对象不拥有(无引用环)
        osg::ref_ptr<osg::Geode> _geode;
        osg::ref_ptr<osg::StateSet> _ss;
        std::vector<earthflight::FlightTrack> _flights, _pending;
        OpenThreads::Mutex _mutex;
        std::atomic<bool> _enabled;   // 主线程 setEnabled() 写 / 抓取线程 run() 读,跨线程
        bool _dirty;                  // 只在 _mutex 保护下跨线程访问(postSnapshot/syncIfDirty),已同步,不需 atomic
        double _bbLatMin, _bbLonMin, _bbLatMax, _bbLonMax;
        OpenThreads::Mutex _bbMutex;
        FetchThread* _thread;
        std::atomic<bool> _refreshNow;   // 主线程 setEnabled() 写 / 抓取线程 takeRefreshNow() 读并清零,跨线程
        double _t0;  // 最近一次快照应用时的 referenceTime,用于插值基准
        FlightInfo _selected;
        mutable OpenThreads::Mutex _selMutex;
    };

    void SyncCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        double refTime = nv->getFrameStamp() ? nv->getFrameStamp()->getReferenceTime() : 0.0;
        _owner->syncIfDirty(refTime);
        _owner->interpolate(refTime);
        traverse(node, nv);
    }

    // FetchThread::run() 定义在 FlightLayerImpl 完整定义之后
    void FetchThread::run()
    {
        const int kIntervalTicks = 120;   // ~12s ÷ 100ms/tick
        int tick = 0;
        while (!_done)
        {
            if (_owner->isEnabled())
            {
                bool force = _owner->takeRefreshNow();
                if (force || tick <= 0)
                {
                    double a, b, c, d; _owner->getBBox(a, b, c, d);
                    _owner->postSnapshot(fetchFlights(a, b, c, d));
                    tick = kIntervalTicks;
                }
                else tick--;
            }
            else tick = 0;   // 关闭时,下次开启立即抓
            OpenThreads::Thread::microSleep(100000);  // 100ms
        }
        _done = true;
    }

    // 视口 bbox 事件处理器:每帧计算相机可见范围,更新 FlightLayerImpl 的 bbox。
    class FlightBBoxHandler : public osgGA::GUIEventHandler
    {
    public:
        FlightBBoxHandler(FlightLayerImpl* o) : _owner(o) {}
        virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
        {
            if (ea.getEventType() != osgGA::GUIEventAdapter::FRAME) return false;
            if (!_owner->isEnabled()) return false;
            osgViewer::View* view = static_cast<osgViewer::View*>(&aa);
            osg::Vec3d eye = view->getCamera()->getInverseViewMatrix().getTrans();
            osg::Vec3d lla = osgVerse::Coordinate::convertECEFtoLLA(eye);  // (latRad, lonRad, altM)
            double lat0 = osg::RadiansToDegrees(lla[0]), lon0 = osg::RadiansToDegrees(lla[1]);
            double h = lla[2]; const double R = 6371000.0;
            double cosv = R / (R + (h > 0.0 ? h : 0.0));
            double thetaDeg = (cosv < 1.0) ? osg::RadiansToDegrees(acos(cosv)) : 0.0;  // 地平线张角
            if (thetaDeg >= 80.0)  // 高空 → 全球
            { _owner->setViewBBox(-85.0, -180.0, 85.0, 180.0); return false; }
            double latMin = lat0 - thetaDeg, latMax = lat0 + thetaDeg;
            double cosLat = cos(osg::DegreesToRadians(lat0)); if (cosLat < 0.2) cosLat = 0.2;
            double lonHalf = thetaDeg / cosLat; if (lonHalf > 180.0) lonHalf = 180.0;
            double lonMin = lon0 - lonHalf, lonMax = lon0 + lonHalf;
            if (latMin < -85.0) latMin = -85.0; if (latMax > 85.0) latMax = 85.0;
            _owner->setViewBBox(latMin, lonMin, latMax, lonMax);
            return false;
        }
    protected:
        FlightLayerImpl* _owner;
    };

    static const float kFlightDragThreshPx2 = 25.0f;   // 5px 拖动阈值的平方

    // 点击拾取处理器:镜像 QuakePickHandler — PUSH 记录坐标,RELEASE 若未拖动则拾取。
    class FlightPickHandler : public osgGA::GUIEventHandler
    {
    public:
        FlightPickHandler(FlightLayerImpl* o) : _owner(o), _downX(0.0f), _downY(0.0f), _pushed(false) {}
        virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
        {
            if (ea.getEventType() == osgGA::GUIEventAdapter::PUSH
                && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            { _downX = ea.getX(); _downY = ea.getY(); _pushed = true; }
            else if (ea.getEventType() == osgGA::GUIEventAdapter::RELEASE
                     && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            {
                float dx = ea.getX() - _downX, dy = ea.getY() - _downY;
                if (_pushed && dx * dx + dy * dy < kFlightDragThreshPx2)   // 点击(非拖动)
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
            return false;   // 不拦截,放行给 manipulator
        }
    protected:
        FlightLayerImpl* _owner; float _downX, _downY; bool _pushed;
    };
}

osg::Node* configureFlightLayer(osgViewer::View& viewer, osg::Node* earthRoot,
                                const std::string& mainFolder, FlightLayer** outLayer)
{
    osg::ref_ptr<FlightLayerImpl> impl = new FlightLayerImpl;
    osg::Group* root = impl->buildScene();
    root->setUserData(impl.get());
    if (outLayer) *outLayer = impl.get();
    impl->startFetch();   // 线程常驻;仅 isEnabled() 时才真正联网
    viewer.addEventHandler(new FlightBBoxHandler(impl.get()));
    viewer.addEventHandler(new FlightPickHandler(impl.get()));
    return root;
}
