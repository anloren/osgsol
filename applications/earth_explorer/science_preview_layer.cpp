#include "science_preview_layer.h"

#include <cstring>
#include <osg/BlendFunc>
#include <osg/CullFace>
#include <osg/Depth>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/Program>
#include <osg/Texture2D>
#include <modeling/Math.h>
#include <pipeline/Pipeline.h>
#include <ScienceQueryService.h>

namespace
{
    constexpr double PREVIEW_ALTITUDE_METERS = 650.0;

    const char* previewVertexShader =
        "VERSE_VS_OUT vec2 scienceUv;\n"
        "void main() {\n"
        "    scienceUv = osg_MultiTexCoord0.st;\n"
        "    gl_Position = VERSE_MATRIX_MVP * osg_Vertex;\n"
        "}\n";

    const char* previewFragmentShader =
        "uniform sampler2D ScienceMap;\n"
        "VERSE_FS_IN vec2 scienceUv;\n"
        "#ifdef VERSE_GLES3\n"
        "layout(location = 0) VERSE_FS_OUT vec4 fragColor;\n"
        "layout(location = 1) VERSE_FS_OUT vec4 fragOrigin;\n"
        "#endif\n"
        "void main() {\n"
        "    vec4 sampleColor = VERSE_TEX2D(ScienceMap, scienceUv);\n"
        "    float edge = min(min(scienceUv.x, scienceUv.y),\n"
        "                     min(1.0 - scienceUv.x, 1.0 - scienceUv.y));\n"
        "    if (edge < 0.006) sampleColor = vec4(1.0, 0.82, 0.18, 0.95);\n"
        "    else if (sampleColor.a < 0.01) discard;\n"
        "#ifdef VERSE_GLES3\n"
        "    fragColor = sampleColor; fragOrigin = vec4(1.0);\n"
        "#else\n"
        "    gl_FragData[0] = sampleColor; gl_FragData[1] = vec4(1.0);\n"
        "#endif\n"
        "}\n";

    osg::StateSet* createPreviewStateSet(osg::Texture2D* texture)
    {
        osg::ref_ptr<osg::Shader> vertex =
            new osg::Shader(osg::Shader::VERTEX, previewVertexShader);
        osg::ref_ptr<osg::Shader> fragment =
            new osg::Shader(osg::Shader::FRAGMENT, previewFragmentShader);
        vertex->setName("ScienceEarthPreview_VS");
        fragment->setName("ScienceEarthPreview_FS");
        osgVerse::Pipeline::createShaderDefinitions(vertex.get(), 100, 130);
        osgVerse::Pipeline::createShaderDefinitions(fragment.get(), 100, 130);

        osg::ref_ptr<osg::Program> program = new osg::Program;
        program->addShader(vertex.get());
        program->addShader(fragment.get());

        osg::StateSet* state = new osg::StateSet;
        state->setAttributeAndModes(program.get(),
            osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        state->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
        state->addUniform(new osg::Uniform("ScienceMap", 0));
        state->setAttributeAndModes(
            new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
            osg::StateAttribute::ON);
        state->setAttributeAndModes(
            new osg::Depth(osg::Depth::ALWAYS, 0.0, 1.0, false),
            osg::StateAttribute::ON);
        state->setAttributeAndModes(
            new osg::CullFace(osg::CullFace::BACK),
            osg::StateAttribute::ON);
        state->setMode(GL_BLEND, osg::StateAttribute::ON);
        state->setMode(GL_CULL_FACE, osg::StateAttribute::ON);
        state->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
        state->setRenderBinDetails(12, "DepthSortedBin");
        return state;
    }

    osg::Node* createArtifactNodeImpl(
        const earthscience::ScienceArtifact& artifact)
    {
        const earthscience::ScienceRasterPayload& raster = artifact.raster;
        if (!raster.rgba || !raster.groundGrid ||
            raster.width <= 0 || raster.height <= 0 ||
            raster.rgba->size() != static_cast<std::size_t>(
                raster.width * raster.height * 4) ||
            raster.groundGrid->columns < 2 ||
            raster.groundGrid->rows < 2 ||
            raster.groundGrid->points.size() != static_cast<std::size_t>(
                raster.groundGrid->columns * raster.groundGrid->rows))
            return nullptr;

        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(raster.width, raster.height, 1,
                             GL_RGBA, GL_UNSIGNED_BYTE);
        std::memcpy(image->data(), raster.rgba->data(), raster.rgba->size());
        image->setInternalTextureFormat(GL_RGBA8);
        image->setOrigin(osg::Image::BOTTOM_LEFT);

        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(image.get());
        texture->setResizeNonPowerOfTwoHint(false);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
        const int columns = raster.groundGrid->columns;
        const int rows = raster.groundGrid->rows;
        vertices->reserve(columns * rows);
        texcoords->reserve(columns * rows);
        for (int y = 0; y < rows; ++y)
        {
            const double v = static_cast<double>(y) / (rows - 1);
            for (int x = 0; x < columns; ++x)
            {
                const double u = static_cast<double>(x) / (columns - 1);
                const earthscience::ScienceGroundPoint& point =
                    raster.groundGrid->points[y * columns + x];
                const osg::Vec3d ecef = osgVerse::Coordinate::convertLLAtoECEF(
                    osg::Vec3d(osg::DegreesToRadians(point.latitude),
                               osg::DegreesToRadians(point.longitude),
                               PREVIEW_ALTITUDE_METERS));
                vertices->push_back(osg::Vec3(ecef));
                texcoords->push_back(osg::Vec2(static_cast<float>(u),
                                               static_cast<float>(v)));
            }
        }

        osg::ref_ptr<osg::DrawElementsUInt> indices =
            new osg::DrawElementsUInt(GL_TRIANGLES);
        indices->reserve((columns - 1) * (rows - 1) * 6);
        const osg::Vec3d first((*vertices)[0]);
        const osg::Vec3d east((*vertices)[1]);
        const osg::Vec3d diagonal((*vertices)[columns + 1]);
        const bool reverseWinding =
            (((east - first) ^ (diagonal - first)) *
             (first + east + diagonal)) < 0.0;
        for (int y = 0; y < rows - 1; ++y)
        {
            for (int x = 0; x < columns - 1; ++x)
            {
                const unsigned int a = y * columns + x;
                const unsigned int b = a + 1;
                const unsigned int c = a + columns;
                const unsigned int d = c + 1;
                indices->push_back(a);
                indices->push_back(reverseWinding ? d : b);
                indices->push_back(reverseWinding ? b : d);
                indices->push_back(a);
                indices->push_back(reverseWinding ? c : d);
                indices->push_back(reverseWinding ? d : c);
            }
        }

        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setVertexArray(vertices.get());
        geometry->setTexCoordArray(0, texcoords.get());
        geometry->addPrimitiveSet(indices.get());
        geometry->setUseDisplayList(false);
        geometry->setUseVertexBufferObjects(true);
        geometry->setCullingActive(false);
        geometry->setStateSet(createPreviewStateSet(texture.get()));

        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->setName("ScienceEarth AlphaEarth artifact");
        geode->addDrawable(geometry.get());
        return geode.release();
    }
}

osg::Node* createSciencePreviewArtifactNode(
    const earthscience::ScienceArtifact& artifact)
{
    return createArtifactNodeImpl(artifact);
}

class SciencePreviewLayer::SyncCallback : public osg::NodeCallback
{
public:
    explicit SyncCallback(SciencePreviewLayer* layer) : _layer(layer) {}

    void operator()(osg::Node* node, osg::NodeVisitor* visitor) override
    {
        SciencePreviewLayer* layer = _layer.get();
        if (layer) layer->syncFromService();
        traverse(node, visitor);
    }

private:
    osg::observer_ptr<SciencePreviewLayer> _layer;
};

SciencePreviewLayer::SciencePreviewLayer(
    earthscience::ScienceQueryService* service)
    : _service(service), _artifactRoot(new osg::Group), _visible(false),
      _hasArtifact(false), _removeRequested(false), _artifactGeneration(0),
      _suppressedGeneration(0)
{
    setName("ScienceEarthPreviewLayer");
    _artifactRoot->setName("ScienceEarthPreviewArtifactRoot");
    addChild(_artifactRoot.get());
    setUpdateCallback(new SyncCallback(this));
    setDataVariance(osg::Object::DYNAMIC);
}

SciencePreviewLayer::~SciencePreviewLayer() = default;

void SciencePreviewLayer::setVisible(bool visible)
{
    _visible.store(visible, std::memory_order_release);
}

bool SciencePreviewLayer::isVisible() const
{
    return _visible.load(std::memory_order_acquire);
}

bool SciencePreviewLayer::hasArtifact() const
{
    return _hasArtifact.load(std::memory_order_acquire);
}

std::uint64_t SciencePreviewLayer::artifactGeneration() const
{
    return _artifactGeneration.load(std::memory_order_acquire);
}

void SciencePreviewLayer::removeArtifact()
{
    _removeRequested.store(true, std::memory_order_release);
}

void SciencePreviewLayer::syncFromService()
{
    _artifactRoot->setNodeMask(isVisible() ? ~0u : 0u);
    if (_removeRequested.exchange(false, std::memory_order_acq_rel))
    {
        _suppressedGeneration.store(
            artifactGeneration(), std::memory_order_release);
        _artifactRoot->removeChildren(0, _artifactRoot->getNumChildren());
        _artifactGeneration.store(0, std::memory_order_release);
        _hasArtifact.store(false, std::memory_order_release);
    }
    if (!_service) return;

    const earthscience::ScienceJobSnapshot snapshot = _service->snapshot();
    if (!snapshot.displayArtifact ||
        snapshot.displayArtifact->generation == 0 ||
        snapshot.displayArtifact->generation == artifactGeneration() ||
        snapshot.displayArtifact->generation ==
            _suppressedGeneration.load(std::memory_order_acquire))
        return;

    osg::ref_ptr<osg::Node> artifact =
        createSciencePreviewArtifactNode(*snapshot.displayArtifact);
    if (!artifact) return;
    _artifactRoot->removeChildren(0, _artifactRoot->getNumChildren());
    _artifactRoot->addChild(artifact.get());
    _artifactGeneration.store(snapshot.displayArtifact->generation,
                              std::memory_order_release);
    _suppressedGeneration.store(0, std::memory_order_release);
    _hasArtifact.store(true, std::memory_order_release);
}
