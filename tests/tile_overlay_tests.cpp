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
#include <fstream>
#include <iterator>
#include <string>
#include <osg/Texture2D>
#include <osgDB/Options>   // TileCallback.h 前必须先包含(否则 osgDB::Options 不完整,已知坑)
#include <readerwriter/TileCallback.h>

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

static std::string readSourceFile(const std::string& relative)
{
    std::ifstream input(std::string(OSGVERSE_SOURCE_DIR) + "/" + relative);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
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

    // ---- 发布状态区分：未发布 != 显式空；当前 NDVI 覆盖克隆 Options 里的陈旧 GIBS ----
    {
        osgVerse::TileManager* manager = osgVerse::TileManager::instance();
        std::string published;
#if defined(HAS_SCIENCE_OVERLAY)
        CHECK(!manager->tryGetLayerPath(osgVerse::TileCallback::OVERLAY, published));
        manager->setLayerPath(osgVerse::TileCallback::OVERLAY, "");
        CHECK(manager->tryGetLayerPath(osgVerse::TileCallback::OVERLAY, published));
        CHECK(published.empty());

        osg::ref_ptr<osgDB::Options> stale = new osgDB::Options("Overlay=gibs");
        manager->setLayerPath(osgVerse::TileCallback::OVERLAY, earthscience::ndviTemplate());
        CHECK(manager->tryGetLayerPath(osgVerse::TileCallback::OVERLAY, published));
        CHECK(published == earthscience::ndviTemplate());
        CHECK(published != stale->getPluginStringData("Overlay"));

        StubImageRequestHandler fakeReader;
        osg::ref_ptr<osgVerse::TileCallback> newChild = new osgVerse::TileCallback(false);
        newChild->setLayerPath(osgVerse::TileCallback::OVERLAY, published);
        newChild->setTileNumber(3, 2, 4);
        bool emptyPath = false;
        osg::ref_ptr<osg::Texture> childOverlay = newChild->createLayerImage(
            osgVerse::TileCallback::OVERLAY, emptyPath, stale.get(), &fakeReader);
        CHECK(!emptyPath);
        CHECK(childOverlay.valid());
        CHECK(fakeReader.requested == 1);
        CHECK(fakeReader.lastFile.find("MODIS_Terra_NDVI_8Day") != std::string::npos);
        CHECK(fakeReader.lastFile.find("VIIRS_SNPP_CorrectedReflectance_TrueColor") ==
              std::string::npos);
#endif

        const std::string tmsSource = readSourceFile("plugins/osgdb_tms/ReaderWriterTMS.cpp");
        CHECK(tmsSource.find("tryGetLayerPath(osgVerse::TileCallback::OVERLAY") !=
              std::string::npos);
        CHECK(tmsSource.find("setLayerPath(osgVerse::TileCallback::OVERLAY, currentOverlayPath)") !=
              std::string::npos);
        CHECK(tmsSource.find("setLayerPath(osgVerse::TileCallback::OVERLAY, overlayPath)") ==
              std::string::npos);
    }

    std::cout << "[tile_overlay_tests] all OK" << std::endl;
    return 0;
}
