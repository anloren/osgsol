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
        "\"content\":{\"uri\":\"west/tileset.json\"}},"
        "{\"boundingVolume\":{\"sphere\":[-100,0,0,60]},\"geometricError\":60,"
        "\"content\":{\"uri\":\"east/tileset.json\"}}]}}}");

    osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(root + ".verse_tiles");
    CHECK(node.valid());
    GraphVisitor visitor;
    node->accept(visitor);
    CHECK(visitor.proxies.size() == 2);
    CHECK(visitor.proxies[0]->getLoadingExternalReferenceMode() ==
          osg::ProxyNode::DEFER_LOADING_TO_DATABASE_PAGER);
    CHECK(visitor.proxies[0]->getCenterMode() == osg::ProxyNode::USER_DEFINED_CENTER);
    CHECK(visitor.proxies[0]->getNumFileNames() == 1);
    CHECK(visitor.proxies[0]->getFileName(0).find("tileset.json.verse_tiles") !=
          std::string::npos);
    CHECK(visitor.proxies[0]->getRadius() > 0.0f);

    ::unlink(root.c_str());
    ::rmdir(dir.c_str());
    std::cout << "[tiles3d_paging_tests] deferred external roots OK\n";
    return 0;
}
