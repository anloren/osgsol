// tests/tile_overlay_tests.cpp — TileCallback 叠加层异步占位纹理单测(暗瓦片回归)
// 背景(2026-07-05「全部」预设暗瓦片根因排查):createLayerImage 走 ImageRequestHandler
// 异步分支时,返回的 Texture2D 不带任何图像数据 —— GL 对 incomplete texture 采样返回
// 不透明黑 (0,0,0,1),scattering_globe.frag 按 overlay alpha 混合,整块瓦片被渲染成
// 黑色,直到异步图像到达(取不到则永久黑)。离屏 E2E 已复现(RainViewer 模板指向
// 秒拒端口 → 全屏地表黑块)。
// 约定:异步分支返回的占位纹理必须自带 1×1 全透明图像 —— 纹理完整可采样、alpha=0
// 混合无效果,"加载中/加载失败"都表现为"暂无叠加层",而不是黑块。
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <osg/MatrixTransform>
#include <osg/PagedLOD>
#include <osg/Texture2D>
#include <osgDB/Registry>
#include <osgDB/Options>   // TileCallback.h 前必须先包含(否则 osgDB::Options 不完整,已知坑)
#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>
#include <osgUtil/UpdateVisitor>
#include <readerwriter/TileCallback.h>
#include <readerwriter/Utilities.h>
#include "../plugins/osgdb_tms/TmsOverlaySelection.h"

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

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
    std::ostringstream checkMessage; \
    checkMessage << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x; \
    throw std::runtime_error(checkMessage.str()); } } while (0)

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

class FixedImageReader : public osgDB::ReaderWriter
{
public:
    explicit FixedImageReader(osg::Image* image) : _image(image)
    { supportsProtocol("https", "Decoded placeholder fixture"); }
    virtual const char* className() const { return "Decoded placeholder fixture reader"; }
    virtual ReadResult readImage(const std::string&, const Options*) const
    {
        if (!_image.valid()) return ReadResult::FILE_NOT_FOUND;
        return new osg::Image(*_image, osg::CopyOp::DEEP_COPY_IMAGES);
    }
private:
    osg::ref_ptr<osg::Image> _image;
};

class TestableTileCallback : public osgVerse::TileCallback
{
public:
    TestableTileCallback() : osgVerse::TileCallback(false) {}
    void forceOverlayStretchedForTest(bool stretched) { _overlayStretched = stretched; }
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

static osg::MatrixTransform* firstProductionTile(osg::Node* root)
{
    osg::Group* group = root ? root->asGroup() : NULL;
    if (!group || group->getNumChildren() == 0) return NULL;
    osg::PagedLOD* lod = dynamic_cast<osg::PagedLOD*>(group->getChild(0));
    if (!lod || lod->getNumChildren() == 0) return NULL;
    return dynamic_cast<osg::MatrixTransform*>(lod->getChild(0));
}

static std::vector<unsigned char> decodeBase64(const std::string& input)
{
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> output;
    unsigned int bits = 0u;
    int bitCount = 0;
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '=') break;
        const size_t value = alphabet.find(input[i]);
        if (value == std::string::npos) continue;
        bits = (bits << 6) | (unsigned int)value;
        bitCount += 6;
        if (bitCount >= 8)
        {
            bitCount -= 8;
            output.push_back((unsigned char)((bits >> bitCount) & 0xffu));
        }
    }
    return output;
}

static const char* googleUnsupportedZoomPngBase64()
{
    // Captured from the default Google label endpoint on 2026-07-18. SHA-256:
    // 1d406fd834a5cbed434471bb02a1d98a6dd89286a7a67826274a296fb871210c
    return
        "iVBORw0KGgoAAAANSUhEUgAAAQAAAAEABAMAAACuXLVVAAAAIVBMVEUAAABHcEz///8mJiZLS0vy8vLe3t5ubm6SkpKxsbHLy8sdleN9AAAAC3RSTlOMAMiVnsXApq62vFT1sPUAAATdSURBVHja7dpBb+JGFAfwl9jEkFP6DaIB28DJQqK7e0OWtpV6QqSJ1JwQ2x56Q0TNYU/IqajUEyLK9orYXe32W/a98TMlKU1Zg8NW++fgyYQX/PPwPDOPQF/t+UEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAHyWgBMq5HGwKaCg868XrANQYY/NACfFAQ42AhA95RB8hoCTIgEHAAAAwP8PUDb2MX3sVVu1f30q7m8LqOwbYB/HpvMY4LBwQBLSXkfAe/wdKB7QDvjgTM4/cPP8p8sh0Wj++mLs3smPKwDv/eWYKhIcd9POjgCDMz685FR8RW7PmCCi0UdjmrEx4WoOOANj/I5rIqLZNO3sBlCRFHR6t/RNQO1G5A76NDJXnml2XgSrI3DU7LiLLvX4wk1HOzsBjOqiCAQxnPELtkMa8aUPuuTI1S4BozkjQkr65AVZZxcA16ZgSUZ7Nu3xu15p0OiUO3Klq4ABP1duUKtKlXrW2QXApqC8LF/jKzmjXF9/FaA5YGesJh2HdFjNOrsALM7s/XQqgB8NUTbAD0fA0XN6TX7a2R2gbOy9FssIJO/kjOVmBrifAzpd8i8Xw6yzA4BNQR3mxVhe97i+dgTI5gdHz678ZWd7gNObp8tBU9Kxs+BeXHsA0BxIuulAxR/qy872gCM/0nuB5wHOapkHputHoN2MPJkESkFt2dkeMMqWY/nhLS8LnFrRwxywIUOZJRs2a/is2tkhwHnjy1rwYpCuBfdGIAXQc/scD5Yc086OlmNsSgH4MgEtOwPZRfD+1Djp/Tz85BMPOp8OsNuAfwJkYgiiJwAc+rV7gFJNd8g39KzX/c8zlmpbj0AYrAMcyeLYqj8FoDobrgHYX1SaTwJon6Xns6UA14i15crn+bLv4bWIkvH1xS1ljVYN8fy6YcNfn3+UteHuYpgnB6pewwLSUqCiRUBJEzAD/G63/9po1RD/aRoS/jXn6/c2bRu5RkDGjY9aCiyTMPgtWgH4Q5frFm00NDZXNnzwjr7zOerWSUwuAK+lfNRSIHtTr/la//gbwL8t1bNGQ+PQ5oCE8PZcEsbLB6iEctRSYJlV376RGi0D9O0mVRsNlR00hx/Je5ZM7YbN5MkBcs6jVtXRUmAlrZ/NgiVgap9MmyxUdiEcfmj3KfPWad67gNNn2qq6Wgqs3lc8osu7gLPc1yYLVUC6UZrbimKRD3BUE0BaCihgMU534CsAGQHbZKHZCKR/YSuKnCPgNvmopYACkrkCAhkI2+USUBsNVUBaTo5b+XOA/zypkpYCCpAM5zNHPO50zICqzXttNFQBZV82qJFUFOV8dwG/qKlmpUAp1A8LfiU3adi7+07mgY7c+dpoqAWEXNO8pV/qUlE4SS8nwGOAlgKVdCqmmX5qx22NAXxP8sZBGw0VgIS/TCN5JqzPcgJoUM1KAed9CnAnvcsb+4HQDx25DSfBDWWNhgpAwp2JLSXks6QcgM0eyXS12cOeEAAAUBkBUAQA/z0v8j3Y7AsMe/8KR3FDsOmXWIoSbPw1HnyTCgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAODLAvwFbcz575V9+KYAAAAASUVORK5CYII=";
}

static std::string cacheFileFor(const std::string& root, const std::string& url)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16)
        << (unsigned long long)std::hash<std::string>()(url);
    const std::string hash = out.str();
    return root + "/" + hash.substr(0, 2) + "/" + hash + ".tile";
}

static void cacheBytes(const std::string& root, const std::string& url,
                       const std::vector<unsigned char>& bytes)
{
    const std::string path = cacheFileFor(root, url);
    CHECK(osgDB::makeDirectory(osgDB::getFilePath(path)));
    std::ofstream out(path.c_str(), std::ios::binary);
    CHECK(out.good());
    out.write((const char*)bytes.data(), bytes.size());
    CHECK(out.good());
    std::ofstream mime((path + ".mime").c_str());
    mime << "image/png";
}

static void setTileCacheRoot(const std::string& root)
{
#if defined(_WIN32)
    CHECK(_putenv_s("EARTH_TILE_CACHE", root.c_str()) == 0);
#else
    CHECK(setenv("EARTH_TILE_CACHE", root.c_str(), 1) == 0);
#endif
}

#if defined(__APPLE__)
static size_t processThreadCount()
{
    thread_act_array_t threads = NULL;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) return 0;
    for (mach_msg_type_number_t i = 0; i < count; ++i)
        mach_port_deallocate(mach_task_self(), threads[i]);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                  static_cast<vm_size_t>(count) * sizeof(thread_t));
    return static_cast<size_t>(count);
}
#endif

static int runTests()
{
    // ---- 精确的 Google“Zoom Level Not Supported”响应必须始终视为无瓦片 ----
    // 生产缓存证明同一占位 PNG 的请求 URL 不一定保持默认 mt1 模板形式。只要完整
    // SHA-256 与已知占位图相同，就必须拒绝；正常 PNG 仍不得误拒。缓存中的占位图
    // 保留为 negative cache，避免重抓。
    {
        const std::vector<unsigned char> unsupported =
            decodeBase64(googleUnsupportedZoomPngBase64());
        CHECK(unsupported.size() == 1370u);
        const std::string googleLabels =
            "https://mt1.google.com/vt/lyrs=h&x=1&y=2&z=99";
        const std::string googleBase =
            "https://mt1.google.com/vt/lyrs=s&x=1&y=2&z=99";
        const std::string custom =
            "https://tiles.example.invalid/vt/lyrs=h&x=1&y=2&z=99";
        const std::string esri =
            "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/"
            "MapServer/tile/99/2/1";

        CHECK(osgVerse::isUnsupportedGoogleZoomTile(googleLabels, unsupported));
        CHECK(osgVerse::isUnsupportedGoogleZoomTile(googleBase, unsupported));
        CHECK(osgVerse::isUnsupportedGoogleZoomTile(custom, unsupported));
        CHECK(osgVerse::isUnsupportedGoogleZoomTile(esri, unsupported));
        std::vector<unsigned char> changed = unsupported;
        changed[changed.size() / 2] ^= 1u;
        CHECK(!osgVerse::isUnsupportedGoogleZoomTile(googleLabels, changed));

        const char* tempRootEnv = std::getenv("TMPDIR");
        if (!tempRootEnv || !tempRootEnv[0]) tempRootEnv = std::getenv("TEMP");
        const std::string cacheRoot = std::string(
            (tempRootEnv && tempRootEnv[0]) ? tempRootEnv : ".") +
            "/osgverse_tile_overlay_" +
            std::to_string((long long)std::chrono::high_resolution_clock::now()
                               .time_since_epoch().count());
        setTileCacheRoot(cacheRoot);
        cacheBytes(cacheRoot, googleLabels, unsupported);
        cacheBytes(cacheRoot, custom, unsupported);
        cacheBytes(cacheRoot, esri, unsupported);
        std::vector<unsigned char> normalPng = {0x89, 0x50, 0x4e, 0x47, 1, 2, 3};
        const std::string normalGoogle =
            "https://mt1.google.com/vt/lyrs=h&x=1&y=2&z=18";
        cacheBytes(cacheRoot, normalGoogle, normalPng);

        std::string mime, encoding;
        CHECK(osgVerse::loadFileData(googleLabels, mime, encoding).empty());
        CHECK(osgVerse::loadFileData(custom, mime, encoding).empty());
        CHECK(osgVerse::loadFileData(esri, mime, encoding).empty());
        CHECK(osgVerse::loadFileData(normalGoogle, mime, encoding) == normalPng);
        CHECK(osgDB::fileExists(cacheFileFor(cacheRoot, googleLabels)));
        std::cout << "[tile_overlay_tests] Google unsupported-zoom negative cache OK"
                  << std::endl;
    }

    // ---- 已解码的 Google 占位图也必须在贴到瓦片前拒绝 ----
    // osgDB FileCache 或另一 https ReaderWriter 可以绕过 loadFileData 的原始字节过滤；
    // 这里模拟该生产路径，确保 Google 占位图不成为底图纹理，自定义源的相同图像不误伤。
#if defined(OSGVERSE_IMAGE_PLUGIN_PATH)
    {
        CHECK(osgDB::Registry::instance()->loadLibrary(OSGVERSE_IMAGE_PLUGIN_PATH) !=
              osgDB::Registry::NOT_LOADED);
        osgDB::ReaderWriter* imageReader =
            osgDB::Registry::instance()->getReaderWriterForExtension("verse_image");
        CHECK(imageReader != NULL);

        const std::vector<unsigned char> unsupported =
            decodeBase64(googleUnsupportedZoomPngBase64());
        std::string encoded((const char*)unsupported.data(), unsupported.size());
        std::stringstream stream(encoded, std::ios::in | std::ios::binary);
        osg::ref_ptr<osgDB::Options> decodeOptions = new osgDB::Options;
        decodeOptions->setPluginStringData("STREAM_FILENAME", "unsupported.png");
        osg::ref_ptr<osg::Image> decoded =
            imageReader->readImage(stream, decodeOptions.get()).takeImage();
        CHECK(decoded.valid());
        CHECK(decoded->s() == 256 && decoded->t() == 256);

        osg::ref_ptr<FixedImageReader> fixture = new FixedImageReader(decoded.get());
        osgDB::Registry::instance()->addReaderWriter(fixture.get());

        osg::ref_ptr<osgVerse::TileCallback> google =
            new osgVerse::TileCallback(false);
        google->setLayerPath(osgVerse::TileCallback::ORTHOPHOTO,
            "https://mt1.google.com/vt/lyrs=s&x=1&y=2&z=99");
        google->setTileNumber(1, 2, 99);
        bool emptyPath = false;
        osg::ref_ptr<osg::Texture> rejected = google->createLayerImage(
            osgVerse::TileCallback::ORTHOPHOTO, emptyPath, NULL);
        CHECK(!emptyPath);
        CHECK(!rejected.valid());

        osg::ref_ptr<osgVerse::TileCallback> custom =
            new osgVerse::TileCallback(false);
        custom->setLayerPath(osgVerse::TileCallback::ORTHOPHOTO,
            "https://tiles.example.invalid/vt/lyrs=s&x=1&y=2&z=99");
        custom->setTileNumber(1, 2, 99);
        osg::ref_ptr<osg::Texture> rejectedCustom = custom->createLayerImage(
            osgVerse::TileCallback::ORTHOPHOTO, emptyPath, NULL);
        CHECK(!emptyPath);
        CHECK(!rejectedCustom.valid());

        osgDB::Registry::instance()->removeReaderWriter(fixture.get());
        std::cout << "[tile_overlay_tests] decoded Google unsupported-zoom tile rejected OK"
                  << std::endl;
    }
#else
    CHECK(false && "OSGVERSE_IMAGE_PLUGIN_PATH is required");
#endif

    // ---- 活动可见性不得由 UpdateVisitor 代表 ----
    // UpdateVisitor 会走 PagedLOD 的所有已加载子级；这里的深层瓦片
    // 仅属于高像素范围，拉远后已非当前渲染 LOD。单纯 update traversal
    // 不得因其缓存仍存在而续写“当前正在超缩放”的帧戳。
    {
        osgVerse::TileManager* manager = osgVerse::TileManager::instance();
        manager->markOverlayStretchedPastNative(0u);

        osg::ref_ptr<TestableTileCallback> callback = new TestableTileCallback;
        callback->setLayersDone(true);
        callback->forceOverlayStretchedForTest(true);

        osg::ref_ptr<osg::MatrixTransform> deepTile = new osg::MatrixTransform;
        deepTile->setUpdateCallback(callback.get());
        osg::ref_ptr<osg::PagedLOD> lod = new osg::PagedLOD;
        lod->addChild(deepTile.get(), 1000.0f, FLT_MAX);

        osg::ref_ptr<osg::FrameStamp> frame = new osg::FrameStamp;
        frame->setFrameNumber(100u);
        osgUtil::UpdateVisitor update;
        update.setFrameStamp(frame.get());
        lod->accept(update);

        CHECK(manager->getLastOverlayStretchFrame() == 0u);
        std::cout << "[tile_overlay_tests] inactive deep LOD does not refresh stretch frame OK"
                  << std::endl;

        frame->setFrameNumber(101u);
        osg::NodeVisitor visibleCull(osg::NodeVisitor::CULL_VISITOR,
                                     osg::NodeVisitor::TRAVERSE_ALL_CHILDREN);
        visibleCull.setFrameStamp(frame.get());
        (*callback)(deepTile.get(), &visibleCull);
        CHECK(manager->getLastOverlayStretchFrame() == 101u);
        std::cout << "[tile_overlay_tests] visible cull refreshes stretch frame OK"
                  << std::endl;
    }

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
#if defined(__APPLE__)
        const size_t threadsBeforePlugin = processThreadCount();
#endif
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
        osg::MatrixTransform* tile = firstProductionTile(node.get());
        CHECK(tile != NULL);
        CHECK(tile->getUpdateCallback() != NULL);
        CHECK(tile->getCullCallback() == tile->getUpdateCallback());
        CHECK(fixture->count("MODIS_Terra_NDVI_8Day") > 0);
        CHECK(fixture->count("VIIRS_SNPP_CorrectedReflectance_TrueColor") == 0);

        osgVerse::TileManager::instance()->setLayerPath(osgVerse::TileCallback::OVERLAY, "");
        fixture->clear();
        node = loadProductionTmsTile(tms, staleGibs);
        CHECK(node.valid());
        CHECK(fixture->count("MODIS_Terra_NDVI_8Day") == 0);
        CHECK(fixture->count("VIIRS_SNPP_CorrectedReflectance_TrueColor") == 0);
        node = NULL;
        osgDB::Registry::instance()->removeReaderWriter(fixture.get());
#if defined(__APPLE__)
        const size_t threadsWithPlugin = processThreadCount();
        CHECK(threadsWithPlugin >= threadsBeforePlugin + 8u);
#endif
        CHECK(osgDB::Registry::instance()->closeLibrary(OSGVERSE_TMS_PLUGIN_PATH));
#if defined(__APPLE__)
        const size_t threadsAfterClose = processThreadCount();
        CHECK(threadsAfterClose + 8u <= threadsWithPlugin);
#endif
    }
#else
    CHECK(false && "OSGVERSE_TMS_PLUGIN_PATH is required");
#endif

    std::cout << "[tile_overlay_tests] all OK" << std::endl;
    return 0;
}

int main(int, char**)
{
    try
    {
        return runTests();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
