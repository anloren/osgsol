// tests/tile_overlay_tests.cpp — TileCallback 叠加层异步占位纹理单测(暗瓦片回归)
// 背景(2026-07-05「全部」预设暗瓦片根因排查):createLayerImage 走 ImageRequestHandler
// 异步分支时,返回的 Texture2D 不带任何图像数据 —— GL 对 incomplete texture 采样返回
// 不透明黑 (0,0,0,1),scattering_globe.frag 按 overlay alpha 混合,整块瓦片被渲染成
// 黑色,直到异步图像到达(取不到则永久黑)。离屏 E2E 已复现(RainViewer 模板指向
// 秒拒端口 → 全屏地表黑块)。
// 约定:异步分支返回的占位纹理必须自带 1×1 全透明图像 —— 纹理完整可采样、alpha=0
// 混合无效果,"加载中/加载失败"都表现为"暂无叠加层",而不是黑块。
#include <iostream>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include <osg/Texture2D>
#include <osgDB/Registry>
#include <osgDB/Options>   // TileCallback.h 前必须先包含(否则 osgDB::Options 不完整,已知坑)
#include <readerwriter/TileCallback.h>
#include "../plugins/osgdb_tms/TmsOverlaySelection.h"

#if __has_include("../applications/earth_explorer/science_overlay.h")
#include "../applications/earth_explorer/science_overlay.h"
#define HAS_SCIENCE_OVERLAY 1
#endif

#if __has_include("../applications/earth_explorer/science_image_pager.h")
#include "../applications/earth_explorer/science_image_pager.h"
#define HAS_SCIENCE_IMAGE_PAGER 1
#endif

// Release 构建带 -DNDEBUG 会吞掉 assert —— 用自定义 CHECK 保证断言永远生效
#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)

// 只记录请求、不真正加载 —— 模拟"图像尚未到达"的窗口期(此时纹理就已被绑定渲染)
class StubImageRequestHandler : public osg::NodeVisitor::ImageRequestHandler
{
public:
    StubImageRequestHandler() : requested(0), lastPriority(-1.0) {}
    virtual double getPreLoadTime() const { return 0.0; }
    virtual osg::ref_ptr<osg::Image> readRefImageFile(const std::string&, const osg::Referenced* = 0)
    { return osg::ref_ptr<osg::Image>(); }
    virtual void requestImageFile(const std::string& fileName, osg::Object*, int, double priority,
                                  const osg::FrameStamp*, osg::ref_ptr<osg::Referenced>&,
                                  const osg::Referenced* = 0)
    { lastFile = fileName; lastPriority = priority; requested++; }
    std::string lastFile; int requested; double lastPriority;
};

class FixtureImageReader : public osgDB::ReaderWriter
{
public:
    FixtureImageReader() { supportsProtocol("fixture", "Tile overlay production-path fixture"); }
    virtual const char* className() const { return "Tile overlay fixture reader"; }
    virtual ReadResult readImage(const std::string& path, const Options*) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        requests.push_back(path);
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        image->setColor(osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f), 0, 0, 0);
        return image.release();
    }
    void clear() const { std::lock_guard<std::mutex> lock(mutex); requests.clear(); }
    int count(const std::string& token) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        int n = 0;
        for (size_t i = 0; i < requests.size(); ++i)
            if (requests[i].find(token) != std::string::npos) ++n;
        return n;
    }
    mutable std::mutex mutex;
    mutable std::vector<std::string> requests;
};

static osg::Node* loadProductionTmsTile(osgDB::ReaderWriter* tms,
                                        const std::string& staleOverlay)
{
    osg::ref_ptr<osgDB::Options> options = new osgDB::Options;
    options->setPluginStringData("Orthophoto", "fixture://base/{z}/{x}/{y}.png");
    options->setPluginStringData("Overlay", staleOverlay);
    options->setPluginStringData("UseEarth3D", "0");
    return tms->readNode("0-0-x.verse_tms", options.get()).takeNode();
}

int main(int, char**)
{
    // ---- 异步(irh)分支:占位纹理必须"完整且全透明" ----
    {
        StubImageRequestHandler stub;
        osg::ref_ptr<osgVerse::TileCallback> cb = new osgVerse::TileCallback(false);
        cb->setLayerPath(osgVerse::TileCallback::OVERLAY, "http://radar.invalid/{z}/{x}/{y}.png");
        cb->setTileNumber(3, 2, 4);

        bool emptyPath = false;
        osg::ref_ptr<osg::Texture> tex = cb->createLayerImage(
            osgVerse::TileCallback::OVERLAY, emptyPath, NULL, &stub);
        CHECK(!emptyPath);
        CHECK(stub.requested == 1);                  // 异步请求确实发出
        CHECK(tex.valid());                          // 返回了待填充的纹理

        osg::Texture2D* tex2D = dynamic_cast<osg::Texture2D*>(tex.get());
        CHECK(tex2D != NULL);
        osg::Image* img = tex2D->getImage();
        CHECK(img != NULL);                          // 占位图像必须存在(否则 incomplete → 黑)
        CHECK(img->s() >= 1 && img->t() >= 1);
        CHECK(img->getPixelFormat() == GL_RGBA);     // 必须带 alpha 通道才能"透明"
        osg::Vec4 c = img->getColor(0u, 0u);
        CHECK(c.a() == 0.0f);                        // 全透明:混合权重为 0,不影响底图
        CHECK(stub.lastPriority == 4.0);              // 粗层级优先于细层级合并
        std::cout << "[tile_overlay_tests] async placeholder texture OK: "
                  << stub.lastFile << std::endl;
    }

    // ---- 空路径分支行为不变:不发请求、不返回纹理 ----
    {
        StubImageRequestHandler stub;
        osg::ref_ptr<osgVerse::TileCallback> cb = new osgVerse::TileCallback(false);
        cb->setTileNumber(0, 0, 1);   // 未设置 OVERLAY 路径
        bool emptyPath = false;
        osg::ref_ptr<osg::Texture> tex = cb->createLayerImage(
            osgVerse::TileCallback::OVERLAY, emptyPath, NULL, &stub);
        CHECK(emptyPath);
        CHECK(!tex.valid());
        CHECK(stub.requested == 0);
        std::cout << "[tile_overlay_tests] empty-path branch unchanged OK" << std::endl;
    }

    // ---- 科学图层原生层级与专用 ImagePager 并发池 ----
#if defined(HAS_SCIENCE_OVERLAY)
    CHECK(earthscience::nativeMaxZoom(earthscience::ndviTemplate()) == 9);
    CHECK(earthscience::nativeMaxZoom(earthscience::nightlightsTemplate()) == 8);
    CHECK(earthscience::nativeMaxZoom("gebco") == 8);
#else
    CHECK(false && "science_overlay.h is required");
#endif

#if defined(HAS_SCIENCE_IMAGE_PAGER)
    {
        osg::ref_ptr<earthscience::ScienceImagePager> pager =
            new earthscience::ScienceImagePager(8);
        CHECK(pager->getNumImageThreads() == 8);
    }
#else
    CHECK(false && "science_image_pager.h is required");
#endif

    // ---- 真实生产 seam：未发布回退 Options；发布 NDVI 只请求一次；
    //      显式空不回退 ----
    {
        osgVerse::TileManager* manager = osgVerse::TileManager::instance();
#if defined(HAS_SCIENCE_OVERLAY)
        osg::ref_ptr<osgDB::Options> stale = new osgDB::Options;
        stale->setPluginStringData("Overlay", "gibs");
        const std::string staleGibs = stale->getPluginStringData("Overlay");
        const std::string currentNdvi = earthscience::ndviTemplate();

        StubImageRequestHandler request;
        osg::ref_ptr<osgVerse::TileCallback> child = new osgVerse::TileCallback(false);
        osgVerse::applyTmsOverlaySelection(*child, *manager, staleGibs);
        child->setTileNumber(3, 2, 4);
        bool emptyPath = false;
        osg::ref_ptr<osg::Texture> overlay = child->createLayerImage(
            osgVerse::TileCallback::OVERLAY, emptyPath, NULL, &request);
        CHECK(!emptyPath);
        CHECK(overlay.valid());
        CHECK(request.requested == 1);
        CHECK(request.lastFile == "gibs");

        manager->setLayerPath(osgVerse::TileCallback::OVERLAY, currentNdvi);
        request = StubImageRequestHandler();
        child = new osgVerse::TileCallback(false);
        osgVerse::applyTmsOverlaySelection(*child, *manager, staleGibs);
        child->setTileNumber(3, 2, 4);
        overlay = child->createLayerImage(
            osgVerse::TileCallback::OVERLAY, emptyPath, NULL, &request);
        CHECK(!emptyPath);
        CHECK(overlay.valid());
        CHECK(request.requested == 1);
        CHECK(request.lastFile.find("MODIS_Terra_NDVI_8Day") != std::string::npos);
        CHECK(request.lastFile != "gibs");

        manager->setLayerPath(osgVerse::TileCallback::OVERLAY, "");
        request = StubImageRequestHandler();
        child = new osgVerse::TileCallback(false);
        osgVerse::applyTmsOverlaySelection(*child, *manager, staleGibs);
        child->setTileNumber(3, 2, 4);
        overlay = child->createLayerImage(
            osgVerse::TileCallback::OVERLAY, emptyPath, NULL, &request);
        CHECK(emptyPath);
        CHECK(!overlay.valid());
        CHECK(request.requested == 0);
#endif
    }

    // ---- 真实 ReaderWriterTMS::readNode/createTile 调用上述 seam，
    //      防止 wiring 断开 ----
#if defined(OSGVERSE_TMS_PLUGIN_PATH)
    {
        osg::ref_ptr<FixtureImageReader> fixture = new FixtureImageReader;
        osgDB::Registry::instance()->addReaderWriter(fixture.get());
        CHECK(osgDB::Registry::instance()->loadLibrary(OSGVERSE_TMS_PLUGIN_PATH) !=
              osgDB::Registry::NOT_LOADED);
        osgDB::ReaderWriter* tms =
            osgDB::Registry::instance()->getReaderWriterForExtension("verse_tms");
        CHECK(tms != NULL);

        const std::string staleGibs =
            "fixture://VIIRS_SNPP_CorrectedReflectance_TrueColor/{z}/{x}/{y}.png";
        const std::string currentNdvi =
            "fixture://MODIS_Terra_NDVI_8Day/{z}/{x}/{y}.png";

        osgVerse::TileManager::instance()->setLayerPath(
            osgVerse::TileCallback::OVERLAY, currentNdvi);
        fixture->clear();
        osg::ref_ptr<osg::Node> node = loadProductionTmsTile(tms, staleGibs);
        CHECK(node.valid());
        CHECK(fixture->count("MODIS_Terra_NDVI_8Day") > 0);
        CHECK(fixture->count("VIIRS_SNPP_CorrectedReflectance_TrueColor") == 0);

        osgVerse::TileManager::instance()->setLayerPath(osgVerse::TileCallback::OVERLAY, "");
        fixture->clear();
        node = loadProductionTmsTile(tms, staleGibs);
        CHECK(node.valid());
        CHECK(fixture->count("MODIS_Terra_NDVI_8Day") == 0);
        CHECK(fixture->count("VIIRS_SNPP_CorrectedReflectance_TrueColor") == 0);
        osgDB::Registry::instance()->removeReaderWriter(fixture.get());
    }
#else
    CHECK(false && "OSGVERSE_TMS_PLUGIN_PATH is required");
#endif

    std::cout << "[tile_overlay_tests] all OK" << std::endl;
    return 0;
}
