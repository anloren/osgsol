#include <cmath>
#include <cfloat>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <string>
#include <vector>
#include <unistd.h>

#include <osg/NodeVisitor>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osgDB/FileUtils>
#include <osgDB/ReadFile>
#include <osgDB/Registry>

#include "../applications/earth_explorer/tiles3d_data.h"
#include "../plugins/osgdb_3dtiles/PagingUtils.h"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; std::abort(); } } while (0)

namespace
{
    class CommaDecimalPoint : public std::numpunct<char>
    {
    protected:
        char do_decimal_point() const override { return ','; }
    };

    class CountingReader : public osgDB::ReaderWriter
    {
    public:
        CountingReader() : _readCount(0)
        {
            supportsProtocol("counting", "3D Tiles external-read fixture");
        }

        const char* className() const override
        {
            return "3D Tiles counting fixture reader";
        }

        ReadResult readNode(const std::string& path, const Options*) const override
        {
            if (path.find("counting://") == std::string::npos)
                return ReadResult::FILE_NOT_HANDLED;
            ++_readCount;
            if (path.find("counting://rough/") != std::string::npos ||
                path.find("counting://refined/") != std::string::npos)
                return new osg::Node;
            return ReadResult::FILE_NOT_FOUND;
        }

        unsigned int readCount() const { return _readCount; }

    private:
        mutable unsigned int _readCount;
    };

    struct GraphVisitor : public osg::NodeVisitor
    {
        GraphVisitor() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}
        void apply(osg::ProxyNode& node) override
        {
            proxies.push_back(&node);
            traverse(node);
        }
        void apply(osg::PagedLOD& node) override
        {
            pagedLods.push_back(&node);
            traverse(node);
        }
        std::vector<osg::ProxyNode*> proxies;
        std::vector<osg::PagedLOD*> pagedLods;
    };

    std::string makeTempDir()
    {
        char path[] = "/tmp/osgsol-tiles3d-XXXXXX";
        char* made = mkdtemp(path);
        CHECK(made != NULL);
        return made;
    }

    void writeText(const std::string& path, const std::string& text)
    {
        std::ofstream output(path.c_str(), std::ios::binary);
        CHECK(output.good());
        output << text;
        CHECK(output.good());
    }

    osg::ProxyNode* findProxy(const std::vector<osg::ProxyNode*>& proxies,
                              const std::string& fileName)
    {
        for (size_t i = 0; i < proxies.size(); ++i)
        {
            osg::ProxyNode* proxy = proxies[i];
            if (proxy->getNumFileNames() == 1 && proxy->getFileName(0) == fileName)
                return proxy;
        }
        return NULL;
    }

    void checkDeferredProxy(osg::ProxyNode* proxy, const std::string& fileName,
                            const osg::Vec3d& center, double radius,
                            const osgDB::Options* sourceOptions)
    {
        CHECK(proxy != NULL);
        CHECK(proxy->getLoadingExternalReferenceMode() ==
              osg::ProxyNode::DEFER_LOADING_TO_DATABASE_PAGER);
        CHECK(proxy->getCenterMode() == osg::ProxyNode::USER_DEFINED_CENTER);
        CHECK(proxy->getNumFileNames() == 1);
        CHECK(proxy->getFileName(0) == fileName);
        CHECK((proxy->getCenter() - center).length() < 1.0e-6);
        CHECK(std::fabs(proxy->getRadius() - radius) < 1.0e-6);
        CHECK(proxy->getRadius() > 0.0f);
        CHECK(proxy->getBound().valid());
        CHECK(proxy->getBound().radius() > 0.0f);
        const osgDB::Options* proxyOptions =
            dynamic_cast<const osgDB::Options*>(proxy->getDatabaseOptions());
        CHECK(proxyOptions != NULL);
        CHECK(proxyOptions != sourceOptions);
        CHECK(proxyOptions->getPluginStringData("PagingSentinel") == "preserved");
    }

    float readPixelSwitch(const std::string& path, const char* sse)
    {
        osg::ref_ptr<osgDB::Options> options = new osgDB::Options;
        options->setPluginStringData("UsePixelsOnScreen", "1");
        options->setPluginStringData("MaxScreenSpaceError", sse);
        osg::ref_ptr<osg::Node> node =
            osgDB::readNodeFile(path + ".verse_tiles", options.get());
        GraphVisitor visitor;
        if (node.valid()) node->accept(visitor);
        CHECK(node.valid());
        CHECK(visitor.pagedLods.size() == 1);
        CHECK(visitor.pagedLods.front()->getRangeMode() == osg::LOD::PIXEL_SIZE_ON_SCREEN);
        return visitor.pagedLods.front()->getMinRange(1);
    }
}

int main(int, char**)
{
    CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError(NULL) - 8.0) < 1e-9);
    CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("") - 8.0) < 1e-9);
    CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("1") - 2.0) < 1e-9);
    CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("12.5") - 12.5) < 1e-9);
    CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("99") - 32.0) < 1e-9);
    CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("bad") - 8.0) < 1e-9);

    const double preciseSse = std::nextafter(8.0, 9.0);
    const std::locale savedLocale = std::locale();
    std::locale::global(std::locale(std::locale::classic(), new CommaDecimalPoint));
    const std::string serializedSse = earthtiles3d::formatScreenSpaceError(preciseSse);
    std::locale::global(savedLocale);
    CHECK(serializedSse.find(',') == std::string::npos);
    CHECK(serializedSse.find('.') != std::string::npos);
    CHECK(earthtiles3d::resolveScreenSpaceError(serializedSse.c_str()) == preciseSse);

    CHECK(osgDB::Registry::instance()->loadLibrary(OSGVERSE_3DTILES_PLUGIN_PATH) !=
          osgDB::Registry::NOT_LOADED);

    const std::string dir = makeTempDir();
    const std::string root = dir + "/root.json";
    const std::string mixedRoot = dir + "/mixed.json";
    const std::string replaceRoot = dir + "/replace.json";
    const std::string roughRoot = dir + "/rough.json";
    const std::string lodRoot = dir + "/lod.json";
    const std::string addLodRoot = dir + "/add-lod.json";
    writeText(root,
        "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":1000,\"root\":{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,1000]},\"geometricError\":500,"
        "\"refine\":\"ADD\",\"children\":["
        "{\"boundingVolume\":{\"sphere\":[100,0,0,50]},\"geometricError\":50,"
        "\"content\":{\"uri\":\"counting://west/tileset.json\"}},"
        "{\"boundingVolume\":{\"sphere\":[-100,0,0,60]},\"geometricError\":60,"
        "\"content\":{\"uri\":\"counting://east/tileset.json\"}}]}}}");
    writeText(replaceRoot,
        "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":1000,\"root\":{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,1000]},\"geometricError\":500,"
        "\"refine\":\"REPLACE\","
        "\"content\":{\"uri\":\"counting://rough/tileset.json\"},\"children\":["
        "{\"boundingVolume\":{\"sphere\":[0,0,0,100]},\"geometricError\":50,"
        "\"content\":{\"uri\":\"counting://refined/tileset.json\"}}]}}}");
    writeText(mixedRoot,
        "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":1000,\"root\":{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,1000]},\"geometricError\":500,"
        "\"refine\":\"REPLACE\",\"children\":[{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,250]},\"geometricError\":100,"
        "\"content\":{\"uri\":\"counting://rough/mixed.json\"},\"children\":[{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,100]},\"geometricError\":25,"
        "\"content\":{\"uri\":\"counting://refined/local.b3dm\"}}]}]}}}");
    writeText(roughRoot,
        "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":0,\"root\":{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,100]},\"geometricError\":0}}}");
    writeText(lodRoot,
        "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":25,\"root\":{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,100]},\"geometricError\":25,"
        "\"refine\":\"REPLACE\",\"content\":{\"uri\":\"rough.json\"},"
        "\"children\":[{\"boundingVolume\":{\"sphere\":[0,0,0,50]},"
        "\"geometricError\":10,\"content\":{\"uri\":\"refined.json\"}}]}}}");
    writeText(addLodRoot,
        "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":25,\"root\":{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,100]},\"geometricError\":25,"
        "\"refine\":\"ADD\",\"content\":{\"uri\":\"rough.json\"},"
        "\"children\":[{\"boundingVolume\":{\"sphere\":[0,0,0,50]},"
        "\"geometricError\":10,\"content\":{\"uri\":\"refined.json\"}}]}}}");

    osg::ref_ptr<CountingReader> countingReader = new CountingReader;
    osgDB::Registry::instance()->addReaderWriter(countingReader.get());
    osg::ref_ptr<osgDB::Options> rootOptions = new osgDB::Options;
    rootOptions->setPluginStringData("PagingSentinel", "preserved");
    osg::ref_ptr<osg::Node> node =
        osgDB::readNodeFile(root + ".verse_tiles", rootOptions.get());
    GraphVisitor visitor;
    if (node.valid()) node->accept(visitor);
    const unsigned int externalReadAttempts = countingReader->readCount();

    std::cerr << "[tiles3d_paging_tests] external child read attempts: "
              << externalReadAttempts << std::endl;
    CHECK(node.valid());
    CHECK(externalReadAttempts == 0);
    CHECK(visitor.proxies.size() == 2);
    checkDeferredProxy(findProxy(visitor.proxies,
                                 "counting://west/tileset.json.verse_tiles"),
                       "counting://west/tileset.json.verse_tiles",
                       osg::Vec3d(100, 0, 0), 50.0, rootOptions.get());
    checkDeferredProxy(findProxy(visitor.proxies,
                                 "counting://east/tileset.json.verse_tiles"),
                       "counting://east/tileset.json.verse_tiles",
                       osg::Vec3d(-100, 0, 0), 60.0, rootOptions.get());

    std::cout << "[tiles3d_paging_tests] deferred external roots OK\n";

    const unsigned int readsBeforeMixed = countingReader->readCount();
    osg::ref_ptr<osg::Node> mixedNode =
        osgDB::readNodeFile(mixedRoot + ".verse_tiles", rootOptions.get());
    GraphVisitor mixedVisitor;
    if (mixedNode.valid()) mixedNode->accept(mixedVisitor);
    const unsigned int mixedRoughReadAttempts =
        countingReader->readCount() - readsBeforeMixed;

    CHECK(mixedNode.valid());
    CHECK(mixedRoughReadAttempts > 0);
    CHECK(mixedVisitor.pagedLods.size() == 1);
    CHECK(mixedVisitor.proxies.empty());
    osg::PagedLOD* mixedLod = mixedVisitor.pagedLods.front();
    CHECK(mixedLod->getNumChildren() == 1);
    CHECK(mixedLod->getNumFileNames() == 2);
    const osgDB::Options* mixedPagerOptions =
        dynamic_cast<const osgDB::Options*>(mixedLod->getDatabaseOptions());
    CHECK(mixedPagerOptions != NULL);
    CHECK(mixedPagerOptions->getPluginStringData("DeferExternalTilesets") == "0");

    const std::string mixedRefinedPseudoFile =
        mixedLod->getDatabasePath() + mixedLod->getFileName(1);
    const unsigned int readsBeforeMixedRefined = countingReader->readCount();
    osg::ref_ptr<osg::Node> mixedRefinedNode =
        osgDB::readNodeFile(mixedRefinedPseudoFile, mixedPagerOptions);
    const unsigned int mixedRefinedReadAttempts =
        countingReader->readCount() - readsBeforeMixedRefined;
    GraphVisitor mixedRefinedVisitor;
    if (mixedRefinedNode.valid()) mixedRefinedNode->accept(mixedRefinedVisitor);

    CHECK(mixedRefinedNode.valid());
    CHECK(mixedRefinedReadAttempts > 0);
    CHECK(dynamic_cast<osg::ProxyNode*>(mixedRefinedNode.get()) == NULL);
    CHECK(mixedRefinedVisitor.proxies.empty());
    CHECK(mixedRefinedVisitor.pagedLods.empty());

    std::cout << "[tiles3d_paging_tests] mixed rough/refined subtree stayed atomic\n";

    osg::ref_ptr<osg::Node> replaceNode =
        osgDB::readNodeFile(replaceRoot + ".verse_tiles", rootOptions.get());
    GraphVisitor replaceVisitor;
    if (replaceNode.valid()) replaceNode->accept(replaceVisitor);
    const unsigned int roughReadAttempts =
        countingReader->readCount() - externalReadAttempts;

    CHECK(replaceNode.valid());
    CHECK(roughReadAttempts > 0);
    CHECK(replaceVisitor.pagedLods.size() == 1);
    CHECK(replaceVisitor.proxies.empty());
    osg::PagedLOD* replaceLod = replaceVisitor.pagedLods[0];
    CHECK(replaceLod->getNumChildren() == 1);
    CHECK(replaceLod->getNumFileNames() == 2);
    const osgDB::Options* pagerOptions =
        dynamic_cast<const osgDB::Options*>(replaceLod->getDatabaseOptions());
    CHECK(pagerOptions != NULL);

    const std::string refinedPseudoFile =
        replaceLod->getDatabasePath() + replaceLod->getFileName(1);
    const unsigned int readsBeforeRefinedPage = countingReader->readCount();
    osg::ref_ptr<osg::Node> refinedNode =
        osgDB::readNodeFile(refinedPseudoFile, pagerOptions);
    const unsigned int refinedPageReadAttempts =
        countingReader->readCount() - readsBeforeRefinedPage;
    GraphVisitor refinedVisitor;
    if (refinedNode.valid()) refinedNode->accept(refinedVisitor);

    std::cerr << "[tiles3d_paging_tests] REPLACE refined-page read attempts: "
              << refinedPageReadAttempts << std::endl;
    CHECK(refinedNode.valid());
    CHECK(refinedPageReadAttempts > 0);
    CHECK(dynamic_cast<osg::ProxyNode*>(refinedNode.get()) == NULL);
    CHECK(refinedVisitor.proxies.empty());
    CHECK(pagerOptions->getPluginStringData("DeferExternalTilesets") == "0");

    osg::ref_ptr<osgDB::Options> lodOptions = new osgDB::Options;
    lodOptions->setPluginStringData("UsePixelsOnScreen", "1");
    lodOptions->setPluginStringData("MaxScreenSpaceError", "8");
    osg::ref_ptr<osg::Node> lodNode =
        osgDB::readNodeFile(lodRoot + ".verse_tiles", lodOptions.get());
    GraphVisitor lodVisitor;
    if (lodNode.valid()) lodNode->accept(lodVisitor);
    CHECK(lodNode.valid());
    CHECK(lodVisitor.pagedLods.size() == 1);
    osg::PagedLOD* lod = lodVisitor.pagedLods.front();
    std::cerr << "[tiles3d_paging_tests] pixel switch=" << lod->getMinRange(1)
              << ", protected children=" << lod->getNumChildrenThatCannotBeExpired()
              << ", refined expiry=" << lod->getMinimumExpiryTime(1) << std::endl;
    CHECK(lod->getRangeMode() == osg::LOD::PIXEL_SIZE_ON_SCREEN);
    CHECK(lod->getNumChildrenThatCannotBeExpired() == 1);
    CHECK(std::fabs(lod->getMinimumExpiryTime(1) - 30.0) < 1e-9);
    CHECK(std::fabs(lod->getMinRange(1) - 64.0f) < 1e-4f);
    CHECK(std::fabs(lod->getMaxRange(0) - 64.0f) < 1e-4f);
    CHECK(lod->getMaxRange(1) == FLT_MAX);

    osg::ref_ptr<osg::Node> addLodNode =
        osgDB::readNodeFile(addLodRoot + ".verse_tiles", lodOptions.get());
    GraphVisitor addLodVisitor;
    if (addLodNode.valid()) addLodNode->accept(addLodVisitor);
    CHECK(addLodNode.valid());
    CHECK(addLodVisitor.pagedLods.size() == 1);
    osg::PagedLOD* addLod = addLodVisitor.pagedLods.front();
    CHECK(addLod->getRangeMode() == osg::LOD::PIXEL_SIZE_ON_SCREEN);
    CHECK(addLod->getMinRange(0) == 0.0f);
    CHECK(addLod->getMaxRange(0) == FLT_MAX);
    CHECK(std::fabs(addLod->getMinRange(1) - 64.0f) < 1e-4f);
    CHECK(addLod->getMaxRange(1) == FLT_MAX);

    osg::ref_ptr<osgDB::Options> legacyOptions = new osgDB::Options;
    osg::ref_ptr<osg::Node> legacyNode =
        osgDB::readNodeFile(addLodRoot + ".verse_tiles", legacyOptions.get());
    GraphVisitor legacyVisitor;
    if (legacyNode.valid()) legacyNode->accept(legacyVisitor);
    CHECK(legacyNode.valid());
    CHECK(legacyVisitor.pagedLods.size() == 1);
    osg::PagedLOD* legacyLod = legacyVisitor.pagedLods.front();
    const float expectedLegacyRange =
        static_cast<float>(25.0 * 1080.0 / (16.0 * 0.5629));
    CHECK(legacyLod->getRangeMode() == osg::LOD::DISTANCE_FROM_EYE_POINT);
    CHECK(legacyLod->getMinRange(0) == 0.0f);
    CHECK(legacyLod->getMaxRange(0) == FLT_MAX);
    CHECK(legacyLod->getMinRange(1) == 0.0f);
    CHECK(std::fabs(legacyLod->getMaxRange(1) - expectedLegacyRange) < 1e-3f);

    CHECK(std::fabs(readPixelSwitch(lodRoot, "inf") - 1.0f) < 1e-6f);
    const double infinity = std::numeric_limits<double>::infinity();
    CHECK(std::fabs(osgVerse::Tiles3dPaging::computeSwitchPixels(infinity, 25.0, 8.0) -
                    1.0) < 1e-9);
    CHECK(std::fabs(osgVerse::Tiles3dPaging::computeSwitchPixels(100.0, infinity, 8.0) -
                    1.0) < 1e-9);
    CHECK(std::fabs(osgVerse::Tiles3dPaging::computeSwitchPixels(100.0, 25.0, infinity) -
                    1.0) < 1e-9);
    CHECK(std::fabs(osgVerse::Tiles3dPaging::computeSwitchPixels(1e308, 1e-308, 32.0) -
                    1.0) < 1e-9);

    osgDB::Registry::instance()->removeReaderWriter(countingReader.get());
    ::unlink(root.c_str());
    ::unlink(mixedRoot.c_str());
    ::unlink(replaceRoot.c_str());
    ::unlink(lodRoot.c_str());
    ::unlink(addLodRoot.c_str());
    ::unlink(roughRoot.c_str());
    ::rmdir(dir.c_str());
    std::cout << "[tiles3d_paging_tests] REPLACE refined group stayed atomic\n";
    return 0;
}
