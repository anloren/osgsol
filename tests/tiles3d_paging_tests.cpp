#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

#include <osg/NodeVisitor>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osgDB/FileUtils>
#include <osgDB/ReadFile>
#include <osgDB/Registry>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; std::abort(); } } while (0)

namespace
{
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
                            const osg::Vec3d& center, double radius)
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
    }
}

int main(int, char**)
{
    CHECK(osgDB::Registry::instance()->loadLibrary(OSGVERSE_3DTILES_PLUGIN_PATH) !=
          osgDB::Registry::NOT_LOADED);

    const std::string dir = makeTempDir();
    const std::string root = dir + "/root.json";
    writeText(root,
        "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":1000,\"root\":{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,1000]},\"geometricError\":500,"
        "\"refine\":\"ADD\",\"children\":["
        "{\"boundingVolume\":{\"sphere\":[100,0,0,50]},\"geometricError\":50,"
        "\"content\":{\"uri\":\"counting://west/tileset.json\"}},"
        "{\"boundingVolume\":{\"sphere\":[-100,0,0,60]},\"geometricError\":60,"
        "\"content\":{\"uri\":\"counting://east/tileset.json\"}}]}}}");

    osg::ref_ptr<CountingReader> countingReader = new CountingReader;
    osgDB::Registry::instance()->addReaderWriter(countingReader.get());
    osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(root + ".verse_tiles");
    GraphVisitor visitor;
    if (node.valid()) node->accept(visitor);
    const unsigned int externalReadAttempts = countingReader->readCount();
    osgDB::Registry::instance()->removeReaderWriter(countingReader.get());
    ::unlink(root.c_str());
    ::rmdir(dir.c_str());

    std::cerr << "[tiles3d_paging_tests] external child read attempts: "
              << externalReadAttempts << std::endl;
    CHECK(node.valid());
    CHECK(externalReadAttempts == 0);
    CHECK(visitor.proxies.size() == 2);
    checkDeferredProxy(findProxy(visitor.proxies,
                                 "counting://west/tileset.json.verse_tiles"),
                       "counting://west/tileset.json.verse_tiles", osg::Vec3d(100, 0, 0), 50.0);
    checkDeferredProxy(findProxy(visitor.proxies,
                                 "counting://east/tileset.json.verse_tiles"),
                       "counting://east/tileset.json.verse_tiles", osg::Vec3d(-100, 0, 0), 60.0);

    std::cout << "[tiles3d_paging_tests] deferred external roots OK\n";
    return 0;
}
