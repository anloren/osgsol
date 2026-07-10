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
#include <osg/Texture2D>
#include <osgDB/Options>   // TileCallback.h 前必须先包含(否则 osgDB::Options 不完整,已知坑)
#include <readerwriter/TileCallback.h>

// Release 构建带 -DNDEBUG 会吞掉 assert —— 用自定义 CHECK 保证断言永远生效
#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)

// 只记录请求、不真正加载 —— 模拟"图像尚未到达"的窗口期(此时纹理就已被绑定渲染)
class StubImageRequestHandler : public osg::NodeVisitor::ImageRequestHandler
{
public:
    StubImageRequestHandler() : requested(0) {}
    virtual double getPreLoadTime() const { return 0.0; }
    virtual osg::ref_ptr<osg::Image> readRefImageFile(const std::string&, const osg::Referenced* = 0)
    { return osg::ref_ptr<osg::Image>(); }
    virtual void requestImageFile(const std::string& fileName, osg::Object*, int, double,
                                  const osg::FrameStamp*, osg::ref_ptr<osg::Referenced>&,
                                  const osg::Referenced* = 0)
    { lastFile = fileName; requested++; }
    std::string lastFile; int requested;
};

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

    std::cout << "[tile_overlay_tests] all OK" << std::endl;
    return 0;
}
