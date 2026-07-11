#include <osg/CullFace>
#include <osg/Texture2D>
#include <osg/MatrixTransform>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>
#include <osgUtil/SmoothingVisitor>
#include <mutex>

#include <modeling/Math.h>
#include <pipeline/Utilities.h>
#include "Utilities.h"
#include "TileCallback.h"

using namespace osgVerse;

class FindTileGeometry : public osg::NodeVisitor
{
public:
    FindTileGeometry() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}
    osg::observer_ptr<osg::Geometry> geometry;

    virtual void apply(osg::Geode& node)
    {
        for (unsigned int i = 0; i < node.getNumDrawables(); ++i)
        {
            osg::Geometry* geom = node.getDrawable(i)->asGeometry();
            if (geom) apply(*geom);
        }
        traverse(node);
    }

    virtual void apply(osg::Geometry& geom)
    { geometry = &geom; }
};

std::string TileCallback::createPath(const std::string& pseudoPath, int x, int y, int z)
{
    std::string path = pseudoPath; bool changed = false;
    path = replace(path, "{z}", std::to_string(z), changed);
    path = replace(path, "{x}", std::to_string(x), changed);
    path = replace(path, "{y}", std::to_string(y), changed); return path;
}

std::string TileCallback::replace(std::string& src, const std::string& match, const std::string& v, bool& c)
{
    size_t levelPos = src.find(match); if (levelPos == std::string::npos) { c = false; return src; }
    src.replace(levelPos, match.length(), v); c = true; return src;
}

std::pair<osg::Geometry*, TileCallback*> TileCallback::findParentTile(osg::Group* parent)
{
    // Structure:
    /* PLOD_Z0 - Tile_Z0 - Geom (with maps)
               - File_Z0 - PLOD_Z1_00 - Tile_Z1_00 - Geom (current without maps)
                            (parent)  - File_Z1
                         - PLOD_Z1_10 - Tile_Z1_10
                                      - File_Z1
                         - ...
    */
    osg::observer_ptr<osg::Node> lastLvTileNode;
    osg::observer_ptr<osg::Geometry> lastLvTile;
    if (parent && parent->getNumParents() > 0)
    {
        osg::Group* siblingTileGroup = parent->getParent(0);
        if (siblingTileGroup->getNumParents() > 0)
        {
            osg::Group* lastLvPagingNode = siblingTileGroup->getParent(0);
            FindTileGeometry ftg; lastLvTileNode = lastLvPagingNode->getChild(0);
            lastLvTileNode->accept(ftg); lastLvTile = ftg.geometry;
        }
        else
            OSG_NOTICE << "[TileCallback] Failed to find parent pager of tile" << std::endl;
    }
    else
        OSG_NOTICE << "[TileCallback] Failed to vertify pager of tile" << std::endl;

    if (lastLvTileNode.valid() && lastLvTile.valid())
    {
        TileCallback* lastLvCallback = dynamic_cast<TileCallback*>(lastLvTileNode->getUpdateCallback());
        return std::pair<osg::Geometry*, TileCallback*>(lastLvTile.get(), lastLvCallback);
    }
    return std::pair<osg::Geometry*, TileCallback*>();
}

osg::Image* TileCallback::decodeTerrarium(const osg::Image* src)
{
    if (!src) return NULL;
    int w = src->s(), h = src->t();
    osg::ref_ptr<osg::Image> dst = new osg::Image;
    dst->allocateImage(w, h, 1, GL_RED, GL_FLOAT);
    dst->setInternalTextureFormat(GL_R32F);
    float* out = (float*)dst->data();
    for (int t = 0; t < h; ++t)
        for (int s = 0; s < w; ++s)
        {
            osg::Vec4 c = src->getColor(osg::Vec2(((float)s + 0.5f) / (float)w,
                                                  ((float)t + 0.5f) / (float)h));
            unsigned char r = (unsigned char)(c.r() * 255.0f + 0.5f);
            unsigned char g = (unsigned char)(c.g() * 255.0f + 0.5f);
            unsigned char b = (unsigned char)(c.b() * 255.0f + 0.5f);
            out[t * w + s] = decodeTerrariumHeight(r, g, b);
        }
    return dst.release();
}

osg::Texture* TileCallback::createLayerImage(LayerType id, bool& emptyPath, const osgDB::Options* opt,
                                             osg::NodeVisitor::ImageRequestHandler* irh)
{
    std::map<int, DataPathPair>::iterator _lpit = _layerPaths.find((int)id);
    std::string inputAddr = (_lpit != _layerPaths.end()) ? _lpit->second.first : std::string();
    emptyPath = (inputAddr.empty()); if (emptyPath) return NULL;

    std::string url = _createPathFunc ? _createPathFunc((int)id, inputAddr, _x, _y, _z)
                    : TileCallback::createPath(inputAddr, _x, _y, _z);
    // path function returning "" (e.g. layer with a per-zoom cutoff like GIBS z>9 / mask z>3)
    // is an INTENTIONAL empty path, not a load failure → mark emptyPath so callers skip fail-state + warning
    std::string protocol = osgDB::getServerProtocol(url); if (url.empty()) { emptyPath = true; return NULL; }

    osg::ref_ptr<osg::Texture2D> tex2D = createTexture2D(NULL, osg::Texture::CLAMP_TO_EDGE);
#if OSG_MIN_VERSION_REQUIRED(3, 1, 5)
    if (irh != NULL)
    {
        // 异步窗口期必须挂 1×1 全透明占位图像:无图像的纹理是 GL incomplete,采样返回
        // 不透明黑 (0,0,0,1),globe 着色器按 overlay alpha 混合会把整块瓦片渲成黑色
        // (「全部」预设暗瓦片 bug 根因)。占位 alpha=0 → 加载中/加载失败都只是"暂无
        // 叠加层";真实图像到达后由 ImagePager 经 setImage 替换。
        // 注意:透明占位仅对"颜色叠加"类图层(OVERLAY/USER)语义正确;ELEVATION 若未来
        // 走 irh,占位会被 mapAltitude 烘成平地且 setImage 不触发几何重建,需另行设计。
        osg::ref_ptr<osg::Image> placeholder = new osg::Image;
        placeholder->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        placeholder->setColor(osg::Vec4(0.0f, 0.0f, 0.0f, 0.0f), 0, 0, 0);
        tex2D->setImage(placeholder.get());
        irh->requestImageFile(url, tex2D.get(), 0, (double)_z, NULL, _imageRequests[url]);
    }
    else
#endif
    {
        osgDB::ReaderWriter* rw = TileManager::instance()->getReaderWriter(protocol, url);
        osg::ref_ptr<osg::Image> image = rw ? rw->readImage(url, opt).takeImage() : NULL;
        //osg::ref_ptr<osg::Image> image = osgDB::readImageFile(url, opt);
        if (!image) return NULL;
        if (id == ELEVATION && _elevationEncoding == TERRARIUM_ELEVATION &&
            image->getDataType() == GL_UNSIGNED_BYTE)
        {
            osg::ref_ptr<osg::Image> decoded = TileCallback::decodeTerrarium(image.get());
            if (decoded.valid()) image = decoded;
            // 应用注入的逐像素高程过滤(区域性修正;默认 NULL 不过滤)
            if (_elevationFilterFunc && image.valid() && image->getDataType() == GL_FLOAT)
                _elevationFilterFunc((float*)image->data(), image->s(), image->t(), _x, _y, _z);
        }
        tex2D->setImage(image.get());
    }
    return tex2D.release();
}

TileGeometryHandler* TileCallback::createLayerHandler(LayerType id, bool& emptyPath, const osgDB::Options* opt)
{
    std::map<int, DataPathPair>::iterator _lpit = _layerPaths.find((int)id);
    std::string inputAddr = (_lpit != _layerPaths.end()) ? _lpit->second.first : std::string();
    emptyPath = (inputAddr.empty()); if (emptyPath) return NULL;

    std::string url = _createPathFunc ? _createPathFunc((int)id, inputAddr, _x, _y, _z)
                    : TileCallback::createPath(inputAddr, _x, _y, _z);
    // path function returning "" (e.g. layer with a per-zoom cutoff like GIBS z>9 / mask z>3)
    // is an INTENTIONAL empty path, not a load failure → mark emptyPath so callers skip fail-state + warning
    std::string protocol = osgDB::getServerProtocol(url); if (url.empty()) { emptyPath = true; return NULL; }

    osgDB::ReaderWriter* rw = TileManager::instance()->getReaderWriter(protocol, url);
    return rw ? dynamic_cast<TileGeometryHandler*>(rw->readObject(url, opt).takeObject()) : NULL;
    //return dynamic_cast<TileGeometryHandler*>(osgDB::readObjectFile(url, opt));
}

double TileCallback::mapAltitude(const osg::Vec4& color, double minH, double maxH) const
{
    double value = minH + (double)color[0] * (maxH - minH);
    return _flatten ? (value * 0.0002) : value;
}

osg::Vec3d TileCallback::adjustLatitudeLongitudeAltitude(const osg::Vec3d& extent, bool useSphericalMercator) const
{
    osg::Vec3d lla(osg::inDegrees(extent[1]), osg::inDegrees(extent[0]), extent[2]);
    if (useSphericalMercator)
    {
        double n = 2.0 * lla.x();
        double adjustedLatitude = atan(0.5 * (exp(n) - exp(-n)));
        return osg::Vec3d(adjustedLatitude, lla.y(), lla.z());
    }
    return lla;
}

void TileCallback::computeTileExtent(osg::Vec3d& tileMin, osg::Vec3d& tileMax,
                                     double& tileWidth, double& tileHeight) const
{
    double multiplier = pow(0.5, double(_z));
    tileWidth = multiplier * (_extentMax.x() - _extentMin.x());
    tileHeight = multiplier * (_extentMax.y() - _extentMin.y());
    if (!_bottomLeft)
    {
        osg::Vec3d origin(_extentMin.x(), _extentMax.y(), _extentMin.z());
        tileMin = origin + osg::Vec3d(double(_x) * tileWidth, -double(_y + 1) * tileHeight, 0.0);
        tileMax = origin + osg::Vec3d(double(_x + 1) * tileWidth, -double(_y) * tileHeight, 1.0);
    }
    else
    {
        tileMin = _extentMin + osg::Vec3d(double(_x) * tileWidth, double(_y) * tileHeight, 0.0);
        tileMax = _extentMin + osg::Vec3d(double(_x + 1) * tileWidth, double(_y + 1) * tileHeight, 1.0);
    }
}

osg::Geometry* TileCallback::createTileGeometry(osg::Matrix& outMatrix, TileGeometryHandler* handler,
                                                const osg::Vec3d& tileMin, const osg::Vec3d& tileMax,
                                                double width, double height) const
{
    if (!_flatten)
    {
        osg::Vec3d center = adjustLatitudeLongitudeAltitude((tileMin + tileMax) * 0.5, _useWebMercator);
        osg::Matrix localToWorld = Coordinate::convertLLAtoENU(center);
        outMatrix = localToWorld;
    }
    return handler ? handler->create(this, outMatrix, tileMin, tileMax, width, height) : NULL;
}

osg::Geometry* TileCallback::createTileGeometry(osg::Matrix& outMatrix, osg::Texture* elevationTex,
                                                const osg::Vec3d& tileMin, const osg::Vec3d& tileMax,
                                                double width, double height,
                                                const osg::Vec4& elevScaleBias) const
{
    osg::Image* elevation = (elevationTex ? elevationTex->getImage(0) : NULL);
    bool useRealElevation = elevation ? (elevation->getDataType() == GL_FLOAT) : false;
    const unsigned int numRows = terrainGridSizeForLevel(_z);
    const unsigned int numCols = numRows;
    unsigned int numVertices = numCols * numRows;
    if (!_flatten && _skirtRatio > 0.0f) numVertices += 2 * (numCols + numRows);

    osg::ref_ptr<osg::Vec3Array> va = new osg::Vec3Array(numVertices);
    osg::ref_ptr<osg::Vec3Array> na = new osg::Vec3Array(numVertices);
    osg::ref_ptr<osg::Vec2Array> ta = new osg::Vec2Array(numVertices);
    osg::ref_ptr<osg::Vec4Array> ca = new osg::Vec4Array(numVertices);
    if (!_flatten)
    {
        osg::Vec3d center = adjustLatitudeLongitudeAltitude((tileMin + tileMax) * 0.5, _useWebMercator);
        osg::Matrix localToWorld = Coordinate::convertLLAtoENU(center); outMatrix = localToWorld;
        osg::Matrix normalMatrix(localToWorld(0, 0), localToWorld(0, 1), localToWorld(0, 2), 0.0,
                                 localToWorld(1, 0), localToWorld(1, 1), localToWorld(1, 2), 0.0,
                                 localToWorld(2, 0), localToWorld(2, 1), localToWorld(2, 2), 0.0,
                                 0.0, 0.0, 0.0, 1.0);

        TileCallback* nonconst = const_cast<TileCallback*>(this);
        nonconst->_worldToLocal = osg::Matrix::inverse(localToWorld);
        nonconst->_elevationRef = (elevation != NULL) ? createTexture2D(new osg::Image(*elevation)) : NULL;

        // FIXME: support compute elevation in shaders?
        double invW = width / (float)(numCols - 1), invH = height / (float)(numRows - 1), lastAlt = 0.0;
        for (unsigned int y = 0; y < numRows; ++y)
            for (unsigned int x = 0; x < numCols; ++x)
            {
                unsigned int vi = x + y * numCols; double altitude = 0.0;
                osg::Vec2 uv((double)x * invW / width, (double)y * invH / height);
                if (elevation)
                {
                    osg::Vec2 euv(uv[0] * elevScaleBias[2] + elevScaleBias[0],
                                  uv[1] * elevScaleBias[3] + elevScaleBias[1]);  // 深瓦片采 z15 祖先子区;ortho 仍用 uv
                    osg::Vec4 elevColor = elevation->getColor(euv);
                    if (elevColor[0] > 10e6 || elevColor[0] < -10e6) { altitude = lastAlt; }
                    else altitude = (useRealElevation ? elevColor[0] : mapAltitude(elevColor)) * _elevationScale;
                }

                osg::Vec3d lla = adjustLatitudeLongitudeAltitude(
                    tileMin + osg::Vec3d((double)x * invW, (double)y * invH, altitude), _useWebMercator);
                osg::Vec3d ecef = convertToECEF(lla); lastAlt = altitude;

                (*va)[vi] = osg::Vec3(ecef * _worldToLocal); (*ta)[vi] = osg::Vec2(uv[0], uv[1]);
                (*na)[vi] = osg::Vec3(normalMatrix.postMult(ecef)); (*na)[vi].normalize();
                
                // For ocean plane, save height difference when ALTITUDE = 0
                if (_withGlobeAttr)
                {
                    lla = adjustLatitudeLongitudeAltitude(
                        tileMin + osg::Vec3d((double)x * invW, (double)y * invH, 0.0), _useWebMercator);
                    osg::Vec3 v0 = convertToECEF(lla); if (altitude >= 0.0) v0 = ecef;
                    (*ca)[vi] = osg::Vec4(v0 * _worldToLocal, 0.0f);
                }
            }
#if false
        for (unsigned int y = 1; y < numRows - 1; ++y)
            for (unsigned int x = 1; x < numCols - 1; ++x)
            {   // Recompute non-boundary normals
                unsigned int vi = x + y * numCols; const osg::Vec3& v0 = (*va)[vi];
                unsigned int vx0 = (x > 0 ? (x - 1) : x) + y * numCols;
                unsigned int vx1 = (x < numCols - 1 ? (x + 1) : x) + y * numCols;
                unsigned int vy0 = x + (y > 0 ? (y - 1) : y) * numCols;
                unsigned int vy1 = x + (y < numCols - 1 ? (y + 1) : y) * numCols;

                osg::Plane p0(v0, (*va)[vx0], (*va)[vy0]), p1(v0, (*va)[vx1], (*va)[vy1]);
                osg::Plane p2(v0, (*va)[vy0], (*va)[vx1]), p3(v0, (*va)[vy1], (*va)[vx0]);
                osg::Vec3 N = p0.getNormal() + p1.getNormal() + p2.getNormal() + p3.getNormal(); N.normalize();
                (*na)[vi] = (*na)[vi] * 0.7f + N * 0.3f; (*na)[vi].normalize();
            }
#endif
    }
    else
    {
        double invW = width / (float)(numCols - 1), invH = height / (float)(numRows - 1), lastAlt = 0.0;
        double elevationScale2D = _elevationScale * 360.0 / 40075017.0;
        for (unsigned int y = 0; y < numRows; ++y)
            for (unsigned int x = 0; x < numCols; ++x)
            {
                unsigned int vi = x + y * numCols; double altitude = 0.0;
                osg::Vec2 uv((double)x * invW / width, (double)y * invH / height);
                if (elevation)
                {
                    osg::Vec2 euv(uv[0] * elevScaleBias[2] + elevScaleBias[0],
                                  uv[1] * elevScaleBias[3] + elevScaleBias[1]);  // 深瓦片采 z15 祖先子区;ortho 仍用 uv
                    osg::Vec4 elevColor = elevation->getColor(euv);
                    if (elevColor[0] > 10e6 || elevColor[0] < -10e6) { altitude = lastAlt; }
                    else altitude = (useRealElevation ? elevColor[0] : mapAltitude(elevColor)) * elevationScale2D;
                }

                osg::Vec3d lla = adjustLatitudeLongitudeAltitude(
                    tileMin + osg::Vec3d((double)x * invW, (double)y * invH, altitude), _useWebMercator);
                (*va)[vi] = osg::Vec3(osg::RadiansToDegrees(lla[1]), osg::RadiansToDegrees(lla[0]), lla[2]);
                (*ta)[vi] = osg::Vec2(uv[0], uv[1]); (*na)[vi] = osg::Z_AXIS; lastAlt = altitude;
                (*ca)[vi] = osg::Vec4((*va)[vi][0], (*va)[vi][1], 0.0f, 0.0f);
            }
    }

    osg::ref_ptr<osg::DrawElementsUShort> de = new osg::DrawElementsUShort(GL_TRIANGLES);
    for (unsigned int y = 0; y < numRows - 1; ++y)
        for (unsigned int x = 0; x < numCols - 1; ++x)
        {
            unsigned int vi = x + y * numCols;
            de->push_back(vi); de->push_back(vi + 1); de->push_back(vi + numCols);
            de->push_back(vi + numCols); de->push_back(vi + 1); de->push_back(vi + numCols + 1);
        }

    osg::Geometry* geom = new osg::Geometry;
    geom->setUserValue("TileGridRows", numRows);
    geom->setUserValue("TileGridColumns", numCols);
    geom->setVertexArray(va.get()); geom->setTexCoordArray(0, ta.get());
    if (_withGlobeAttr)
    {
        geom->setNormalArray(na.get()); geom->setNormalBinding(osg::Geometry::BIND_PER_VERTEX);
        geom->setVertexAttribArray(GLOBE_ATTRIBUTE_INDEX, ca.get());
        geom->setVertexAttribNormalize(GLOBE_ATTRIBUTE_INDEX, GL_FALSE);
        geom->setVertexAttribBinding(GLOBE_ATTRIBUTE_INDEX, osg::Geometry::BIND_PER_VERTEX);
    }
    geom->addPrimitiveSet(de.get());
    if (!_flatten && _skirtRatio > 0.0f)
        updateSkirtData(geom, osg::inDegrees(tileMax.y() - tileMin.y()), true);
    return geom;
}

void TileCallback::updateTileGeometry(osg::Geometry* geom, osg::Texture* elevationTex, const std::string& range,
                                      const osg::Vec3d& tileMin, const osg::Vec3d& tileMax,
                                      double width, double height) const
{
    osg::Image* elevation = (elevationTex ? elevationTex->getImage(0) : NULL);
    bool useRealElevation = elevation ? (elevation->getDataType() == GL_FLOAT) : false;
    unsigned int numRows = 16u, numCols = 16u;
    tileGeometryGridSize(geom, numRows, numCols);
    std::map<std::string, osg::Vec4>::const_iterator itr = _uvRangesToSet.find(range);
    osg::Vec4 scaleRange = (itr == _uvRangesToSet.end()) ? osg::Vec4(0.0f, 0.0f, 1.0f, 1.0f) : itr->second;

    osg::ref_ptr<osg::Vec3Array> va = static_cast<osg::Vec3Array*>(geom->getVertexArray());
    osg::ref_ptr<osg::Vec3Array> na = static_cast<osg::Vec3Array*>(geom->getNormalArray());
    osg::ref_ptr<osg::Vec4Array> ca = static_cast<osg::Vec4Array*>(geom->getVertexAttribArray(GLOBE_ATTRIBUTE_INDEX));
    if (va->size() < numCols * numRows) return;
    if (!_flatten)
    {
        osg::Matrix localToWorld = osg::Matrix::inverse(_worldToLocal);
        osg::Matrix normalMatrix(localToWorld(0, 0), localToWorld(0, 1), localToWorld(0, 2), 0.0,
                                 localToWorld(1, 0), localToWorld(1, 1), localToWorld(1, 2), 0.0,
                                 localToWorld(2, 0), localToWorld(2, 1), localToWorld(2, 2), 0.0,
                                 0.0, 0.0, 0.0, 1.0);

        double invW = width / (float)(numCols - 1), invH = height / (float)(numRows - 1), lastAlt = 0.0;
        for (unsigned int y = 0; y < numRows; ++y)
            for (unsigned int x = 0; x < numCols; ++x)
            {
                unsigned int vi = x + y * numCols; double altitude = 0.0;
                if (elevation)
                {
                    osg::Vec2 uv((double)x * invW / width, (double)y * invH / height);
                    uv[0] = uv[0] * scaleRange[2] + scaleRange[0];
                    uv[1] = uv[1] * scaleRange[3] + scaleRange[1];

                    osg::Vec4 elevColor = elevation->getColor(uv);
                    if (elevColor[0] > 10e6 || elevColor[0] < -10e6) { altitude = lastAlt; }
                    else altitude = (useRealElevation ? elevColor[0] : mapAltitude(elevColor)) * _elevationScale;
                }

                osg::Vec3d lla = adjustLatitudeLongitudeAltitude(
                    tileMin + osg::Vec3d((double)x * invW, (double)y * invH, altitude), _useWebMercator);
                osg::Vec3d ecef = convertToECEF(lla); (*va)[vi] = osg::Vec3(ecef * _worldToLocal);
                if (na.valid()) { (*na)[vi] = osg::Vec3(normalMatrix.postMult(ecef)); (*na)[vi].normalize(); }

                // For ocean plane, save height difference when ALTITUDE = 0
                if (ca.valid())
                {
                    lla = adjustLatitudeLongitudeAltitude(
                        tileMin + osg::Vec3d((double)x * invW, (double)y * invH, 0.0), _useWebMercator);
                    osg::Vec3 v0 = convertToECEF(lla); if (altitude >= 0.0) v0 = ecef;
                    (*ca)[vi] = osg::Vec4(v0 * _worldToLocal, 0.0f);
                }
            }

        if (_skirtRatio > 0.0f)
            updateSkirtData(geom, osg::inDegrees(tileMax.y() - tileMin.y()), false);
    }
    else
    {
        double invW = width / (float)(numCols - 1), invH = height / (float)(numRows - 1), lastAlt = 0.0;
        double elevationScale2D = _elevationScale * 360.0 / 40075017.0;
        for (unsigned int y = 0; y < numRows; ++y)
            for (unsigned int x = 0; x < numCols; ++x)
            {
                unsigned int vi = x + y * numCols; double altitude = 0.0;
                if (elevation)
                {
                    osg::Vec2 uv((double)x * invW / width, (double)y * invH / height);
                    uv[0] = uv[0] * scaleRange[2] + scaleRange[0];
                    uv[1] = uv[1] * scaleRange[3] + scaleRange[1];

                    osg::Vec4 elevColor = elevation->getColor(uv);
                    if (elevColor[0] > 10e6 || elevColor[0] < -10e6) { altitude = lastAlt; }
                    else altitude = (useRealElevation ? elevColor[0] : mapAltitude(elevColor)) * elevationScale2D;
                }

                osg::Vec3d lla = adjustLatitudeLongitudeAltitude(
                    tileMin + osg::Vec3d((double)x * invW, (double)y * invH, altitude), _useWebMercator);
                (*va)[vi] = osg::Vec3(osg::RadiansToDegrees(lla[1]), osg::RadiansToDegrees(lla[0]), lla[2]);
                if (na.valid()) (*na)[vi] = osg::Z_AXIS; lastAlt = altitude;
                if (ca.valid()) (*ca)[vi] = osg::Vec4((*va)[vi][0], (*va)[vi][1], 0.0f, 0.0f);
            }
    }
    va->dirty(); na->dirty(); if (ca.valid()) ca->dirty(); geom->dirtyBound();
}

void TileCallback::updateSkirtData(osg::Geometry* geom, double tileRefSize, bool addingTriangles) const
{
    double skirtHeight = osg::WGS_84_RADIUS_POLAR * tileRefSize * _skirtRatio;
    unsigned int numRows = 16u, numCols = 16u;
    tileGeometryGridSize(geom, numRows, numCols);
    unsigned int vi = numRows * numCols;
    if (!geom) return; else if (!geom->getVertexArray() || geom->getNumPrimitiveSets() == 0) return;

    osg::Vec3Array* va = static_cast<osg::Vec3Array*>(geom->getVertexArray());
    osg::Vec3Array* na = static_cast<osg::Vec3Array*>(geom->getNormalArray());
    osg::Vec2Array* ta = static_cast<osg::Vec2Array*>(geom->getTexCoordArray(0));
    osg::Vec4Array* ca = static_cast<osg::Vec4Array*>(geom->getVertexAttribArray(GLOBE_ATTRIBUTE_INDEX));
    osg::DrawElementsUShort* de = static_cast<osg::DrawElementsUShort*>(geom->getPrimitiveSet(0));
    va->dirty(); ta->dirty(); if (na) na->dirty(); if (ca) ca->dirty(); geom->dirtyBound();
    if (addingTriangles) { de->dirty(); }

    // row[0]
    unsigned int tile_bottom_row = 0, skirt_bottom_row = vi;
    for (unsigned int c = 0; c < numCols; ++c, ++vi)
    {
        unsigned int si = tile_bottom_row + c; osg::Vec3 N = na ? na->at(si) : osg::Z_AXIS; N.normalize();
        va->at(vi) = va->at(si) - N * skirtHeight; if (na) na->at(vi) = N;
        ta->at(vi) = ta->at(si); if (ca) { ca->at(vi) = ca->at(si); ca->at(vi).w() = -1.0f; }
    }
    if (addingTriangles)
    {
        for (unsigned int c = 0; c < numCols - 1; ++c)
        {
            unsigned int tile_i = tile_bottom_row + c, skirt_i = skirt_bottom_row + c;
            de->push_back(tile_i); de->push_back(skirt_i); de->push_back(skirt_i + 1);
            de->push_back(skirt_i + 1); de->push_back(tile_i + 1); de->push_back(tile_i);
        }
    }
    // row[numRows-1]
    unsigned int tile_top_row = (numRows - 1) * numCols, base_top_row = vi;
    for (unsigned int c = 0; c < numCols; ++c, ++vi)
    {
        unsigned int si = tile_top_row + c; osg::Vec3 N = na ? na->at(si) : osg::Z_AXIS; N.normalize();
        va->at(vi) = va->at(si) - N * skirtHeight; if (na) na->at(vi) = N;
        ta->at(vi) = ta->at(si); if (ca) { ca->at(vi) = ca->at(si); ca->at(vi).w() = -1.0f; }
    }
    if (addingTriangles)
    {
        for (unsigned int c = 0; c < numCols - 1; ++c)
        {
            unsigned int tile_i = tile_top_row + c, skirt_i = base_top_row + c;
            de->push_back(tile_i); de->push_back(skirt_i + 1); de->push_back(skirt_i);
            de->push_back(skirt_i + 1); de->push_back(tile_i); de->push_back(tile_i + 1);
        }
    }
    // column[0]
    unsigned int tile_left_column = 0, skirt_left_column = vi;
    for (unsigned int r = 0; r < numRows; ++r, ++vi)
    {
        unsigned int si = tile_left_column + r * numCols; osg::Vec3 N = na ? na->at(si) : osg::Z_AXIS; N.normalize();
        va->at(vi) = va->at(si) - N * skirtHeight; if (na) na->at(vi) = N;
        ta->at(vi) = ta->at(si); if (ca) { ca->at(vi) = ca->at(si); ca->at(vi).w() = -1.0f; }
    }
    if (addingTriangles)
    {
        for (unsigned int r = 0; r < numRows - 1; ++r)
        {
            unsigned int tile_i = tile_left_column + r * numCols, skirt_i = skirt_left_column + r;
            de->push_back(tile_i); de->push_back(skirt_i + 1); de->push_back(skirt_i);
            de->push_back(skirt_i + 1); de->push_back(tile_i); de->push_back(tile_i + numCols);
        }
    }
    // column[numColums-1]
    unsigned int tile_right_column = numCols - 1, skirt_right_column = vi;
    for (unsigned int r = 0; r < numRows; ++r, ++vi)
    {
        unsigned int si = tile_right_column + r * numCols; osg::Vec3 N = na ? na->at(si) : osg::Z_AXIS; N.normalize();
        va->at(vi) = va->at(si) - N * skirtHeight; if (na) na->at(vi) = N;
        ta->at(vi) = ta->at(si); if (ca) { ca->at(vi) = ca->at(si); ca->at(vi).w() = -1.0f; }
    }
    if (addingTriangles)
    {
        for (unsigned int r = 0; r < numRows - 1; ++r)
        {
            unsigned int tile_i = tile_right_column + r * numCols, skirt_i = skirt_right_column + r;
            de->push_back(tile_i); de->push_back(skirt_i); de->push_back(skirt_i + 1);
            de->push_back(skirt_i + 1); de->push_back(tile_i + numCols); de->push_back(tile_i);
        }
    }
}

osg::Texture* TileCallback::findAndUseParentData(LayerType id, osg::Group* parent)
{
    std::pair<osg::Geometry*, TileCallback*> parentData = findParentTile(parent);
    osg::observer_ptr<osg::Geometry> lastLvTile = parentData.first;
    osg::observer_ptr<TileCallback> lastLvCallback = parentData.second;
    if (lastLvTile.valid() && lastLvCallback.valid())
    {
        std::string uvRangeName = "UvOffset" + std::to_string((int)id);
        osg::StateSet* ss = lastLvTile->getOrCreateStateSet();

        // Set UV scale value
        // FIXME: should assume _z + 1 = parentZ... And not suitable for no-shader cases
        osg::Uniform* lastUvUniform = ss->getUniform(uvRangeName);
        osg::Vec4 uvRange((float)(_x - lastLvCallback->getTileX() * 2) * 0.5f,
            (float)(_y - lastLvCallback->getTileY() * 2) * 0.5f, 0.5f, 0.5f);
        if (lastUvUniform != NULL)
        {
            osg::Vec4 lastRange; lastUvUniform->get(lastRange);
            uvRange.set(uvRange[0] * lastRange[2] + lastRange[0],
                uvRange[1] * lastRange[3] + lastRange[1],
                uvRange[2] * lastRange[2], uvRange[3] * lastRange[3]);
        }
        _uvRangesToSet[uvRangeName] = uvRange;

        if (id == ELEVATION)
        {
            // TODO: how to generate _elevationRef if tile is created from elevation handler?
            return lastLvCallback->_elevationRef.get();
        }
        else
        {   // Share parent tile's texture data
            int texUnit = -1;
            switch (id)
            {
            case ORTHOPHOTO: texUnit = 0; break;
            case OCEAN_MASK: texUnit = 1; break;
            case OVERLAY: texUnit = 3; break;
            default: texUnit = 2; break;  // USER
            }
            if (texUnit >= 0) return static_cast<osg::Texture*>(
                ss->getTextureAttribute(texUnit, osg::StateAttribute::TEXTURE));
        }
    }
    return NULL;
}

bool TileCallback::updateLayerData(osg::NodeVisitor* nv, osg::Node* node, LayerType id)
{
    FindTileGeometry ftg; node->accept(ftg);
    if (!ftg.geometry) return false;

    osg::ref_ptr<osg::Texture> tex; int texUnit = -1; bool emptyPath = false;
    osg::StateSet* ss = ftg.geometry->getOrCreateStateSet();
    osgDB::Options* opt = TileManager::instance()->getTileLoadingOptions();

    // I-1/I-2 入口复位:每次 OVERLAY 重入(换源/路径变更)都从干净态开始,防止上一个源遗留的
    // 顶替中纹理(_overlayPending)在新源的超缩放拉伸绑定之后到货,把错误旧图 swap 到当前瓦片;
    // 同时清掉上一态的超缩放电平标记,避免切源/禁用后角标残留。随后各分支若命中会重新赋值。
    if (id == OVERLAY) { _overlayPending = NULL; _overlayStretched = false; }

    switch (id)
    {
    case ELEVATION:
        tex = createLayerImage(id, emptyPath, opt); break;
    case ORTHOPHOTO:
        tex = createLayerImage(id, emptyPath, opt); texUnit = 0; break;
    case OCEAN_MASK:
        tex = createLayerImage(id, emptyPath, opt); texUnit = 1; break;
    case OVERLAY:
        // OVERLAY 路径可在运行时切换(降水/云图二选一),check() 会在主线程 update 回调里触发重载。
        // 用 ImageRequestHandler(场景自带的 ImagePager)异步加载,避免主线程同步拉网络瓦片导致整屏卡顿。
        tex = createLayerImage(id, emptyPath, opt, nv ? nv->getImageRequestHandler() : NULL); texUnit = 3; break;
    default:  // USER
        // FIXME: use own ImageRequestHandler if we need to check and reuse parent tile...
        //tex = createLayerImage(id, emptyPath, nv->getImageRequestHandler()); texUnit = 2; break;
        tex = createLayerImage(id, emptyPath, opt); texUnit = 2; break;
    }

    // 异步加载父级顶替:OVERLAY 走异步,createLayerImage 返回的 tex2D 初始是 1×1 透明占位,
    // 真图到货前直接绑上去会"漏地面"。若本瓦片图像尚未就绪且能找到已加载祖先,则先绑父级纹理
    // 顶替(带父级 UV 子区间),把本瓦片 tex 暂存 _overlayPending;operator() 检测到其真图到货后无缝换回。
    if (id == OVERLAY && tex.valid() && node->getNumParents() > 0)
    {
        osg::Texture2D* ownTex = dynamic_cast<osg::Texture2D*>(tex.get());
        osg::Image* ownImg = ownTex ? ownTex->getImage() : NULL;
        bool ownReady = (ownImg && ownImg->s() > 1);  // >1×1 = 真图(可能磁盘缓存同步命中);1×1 = 占位
        if (!ownReady)
        {
            osg::Texture* parentTex = findAndUseParentData(id, node->getParent(0));
            if (parentTex) { _overlayPending = ownTex; tex = parentTex; }  // 先显父级,到货再 swap
        }
    }

    if (!tex && !emptyPath && node->getNumParents() > 0)
        tex = findAndUseParentData(id, node->getParent(0));

    // 超缩放父级拉伸:OVERLAY 超过原生最大缩放时 createCustomPath 返回空(emptyPath),原逻辑会
    // 移除 texUnit 3 → 漏出地面。改为绑最近祖先叠加纹理拉伸顶住(渐糊但始终盖住),并打帧戳供角标。
    // findAndUseParentData 会顺带把父级 UV 子区间排进 _uvRangesToSet,由 operator() 应用。
    if (!tex.valid() && emptyPath && id == OVERLAY && node->getNumParents() > 0)
    {
        tex = findAndUseParentData(id, node->getParent(0));
        if (tex.valid())
        {
            emptyPath = false;  // 视为"有数据",走下方正常绑定路径而非移除分支
            unsigned int fr = (nv && nv->getFrameStamp()) ? nv->getFrameStamp()->getFrameNumber() : 0u;
            TileManager::instance()->markOverlayStretchedPastNative(fr);
            _overlayStretched = true;  // 电平触发:operator() 每帧续帧戳,直到本瓦片不再被遍历或换源
        }
    }
    if (tex.valid())
    {
        std::string uvRangeName = "UvOffset" + std::to_string((int)id);
        if (texUnit < 0 && id == ELEVATION)
        {   // Alter elevation data on the fly
            FindTileGeometry ftg; node->accept(ftg);
            if (ftg.geometry.valid())
            {
                osg::Vec3d tileMin, tileMax; double tileW = 0.0, tileH = 0.0;
                computeTileExtent(tileMin, tileMax, tileW, tileH);
                updateTileGeometry(ftg.geometry.get(), tex.get(), uvRangeName,
                                   tileMin, tileMax, tileW, tileH);
                _elevationRef = tex; return true;  // save ref-elevation for next level...
            }
        }
#if defined(OSG_GLES2_AVAILABLE) || defined(OSG_GLES3_AVAILABLE) || defined(OSG_GL3_AVAILABLE)
        ss->setTextureAttribute(texUnit, tex.get());
#else
        ss->setTextureAttributeAndModes(texUnit, tex.get());
#endif
        if (!ss->getUniform(uvRangeName))
            ss->getOrCreateUniform(uvRangeName, osg::Uniform::FLOAT_VEC4)->set(osg::Vec4(0.0f, 0.0f, 1.0f, 1.0f));
        return true;  // FIXME: for USER layers, use tex2d array instead?
    }
    else if (texUnit >= 0 && emptyPath)
    {
        tex = static_cast<osg::Texture*>(ss->getTextureAttribute(texUnit, osg::StateAttribute::TEXTURE));
        if (tex) ss->removeTextureMode(texUnit, tex->getTextureTarget());
        if (tex) ss->removeTextureAttribute(texUnit, tex); return true;
    }
    else
        OSG_NOTICE << "[TileCallback] Failed to update layer " << id << " of tile " << getName() << "\n";
    return false;
}

void TileCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
{
    if (!_layersDone)
    {
        // Check if current layer paths are all usable and loaded
        for (std::map<int, DataPathPair>::iterator it = _layerPaths.begin();
             it != _layerPaths.end(); ++it)
        {
            DataPathPair& dataPath = it->second; if (dataPath.second == DONE) continue;
            bool loaded = updateLayerData(nv, node, (LayerType)it->first);
            dataPath.second = loaded ? DONE : FAILED;
        }
        _layersDone = true;
    }

    // Check if there are layer changes from the global manager
    std::vector<int> updatedID;
    if (TileManager::instance()->check(_layerPaths, updatedID))
    {
        for (size_t i = 0; i < updatedID.size(); ++i)
        {   // load or update layer image
            LayerType id = (LayerType)updatedID[i];
            _layerPaths[id].first = TileManager::instance()->getLayerPath(id);
            _layerPaths[id].second = updateLayerData(nv, node, id) ? DONE : FAILED;
        }
    }

    // 异步到货 swap:顶替期结束——本瓦片真图已加载(尺寸从 1×1 变真),把 texUnit 3 从父级顶替纹理
    // 换回本瓦片纹理,并把 UV 复位为满铺(0,0,1,1)。UV 通过 _uvRangesToSet 排入,由紧接的应用块生效。
    if (_overlayPending.valid())
    {
        osg::Image* img = _overlayPending->getImage();
        if (img && img->s() > 1)
        {
            FindTileGeometry ftg; node->accept(ftg);
            osg::StateSet* ss = ftg.geometry.valid() ? ftg.geometry->getOrCreateStateSet() : NULL;
            if (ss)
            {
#if defined(OSG_GLES2_AVAILABLE) || defined(OSG_GLES3_AVAILABLE) || defined(OSG_GL3_AVAILABLE)
                ss->setTextureAttribute(3, _overlayPending.get());
#else
                ss->setTextureAttributeAndModes(3, _overlayPending.get());
#endif
                _uvRangesToSet["UvOffset4"] = osg::Vec4(0.0f, 0.0f, 1.0f, 1.0f);
            }
            _overlayPending = NULL;
        }
    }

    // The UvRange uniforms are applied later
    if (!_uvRangesToSet.empty())
    {
        FindTileGeometry ftg; node->accept(ftg);
        osg::StateSet* ss = ftg.geometry.valid() ? ftg.geometry->getOrCreateStateSet() : NULL;
        if (ss)
        {
            for (std::map<std::string, osg::Vec4>::iterator it = _uvRangesToSet.begin();
                 it != _uvRangesToSet.end(); ++it)
            {
                osg::Uniform* u = ss->getUniform(it->first); if (u) u->set(it->second);
                if (!u) ss->getOrCreateUniform(it->first, osg::Uniform::FLOAT_VEC4)->set(it->second);
            }
        }
        _uvRangesToSet.clear();
    }

    // I-2 电平触发续帧戳:只要本瓦片仍处于"超缩放父级拉伸"态且还在被每帧 update 遍历(即仍在
    // 视锥/仍是当前显示的叶瓦片),就持续把帧戳刷新为当前帧,角标去抖窗口(30 帧)因而不会过期。
    // 一旦本瓦片不再被遍历(zoom out、换源或入口复位清掉该态),帧戳停止刷新,角标随去抖自然隐藏。
    if (_overlayStretched)
    {
        unsigned int fr = (nv && nv->getFrameStamp()) ? nv->getFrameStamp()->getFrameNumber() : 0u;
        TileManager::instance()->markOverlayStretchedPastNative(fr);
    }

    if (TileManager::instance()->shouldMorph(*this))
    {
        FindTileGeometry ftg; node->accept(ftg);
        if (ftg.geometry.valid())
            TileManager::instance()->updateTileGeometry(*this, ftg.geometry.get());
    }
    traverse(node, nv);
}

osg::Vec3d TileCallback::convertToECEF(const osg::Vec3d& lla) const
{
    // TODO: consider polar-stereo
    return Coordinate::convertLLAtoECEF(lla);
}

////////////////////////// TileManager //////////////////////////

TileManager* TileManager::instance()
{
    static osg::ref_ptr<TileManager> s_instance = new TileManager;
    return s_instance.get();
}

TileManager::TileManager()
{
    _acceptHandlerExts[".terrain"] = ".terrain.verse_terrain";
    _acceptHandlerExts[".verse_terrain"] = "";
}

bool TileManager::check(const std::map<int, TileCallback::DataPathPair>& paths, std::vector<int>& updated)
{
    std::lock_guard<std::mutex> lock(_layerPathsMutex);
    for (std::map<int, std::string>::const_iterator it = _layerPaths.begin();
         it != _layerPaths.end(); ++it)
    {
        std::map<int, TileCallback::DataPathPair>::const_iterator it2 = paths.find(it->first);
        if (it2 == paths.end() && !it->second.empty()) updated.push_back(it->first);
        else if (it2 != paths.end() && it2->second.first != it->second) updated.push_back(it->first);
    }
    return !updated.empty();
}

void TileManager::setLayerPath(TileCallback::LayerType id, const std::string& path)
{
    std::lock_guard<std::mutex> lock(_layerPathsMutex);
    _layerPaths[id] = path;
}

bool TileManager::tryGetLayerPath(TileCallback::LayerType id, std::string& path) const
{
    std::lock_guard<std::mutex> lock(_layerPathsMutex);
    std::map<int, std::string>::const_iterator it = _layerPaths.find((int)id);
    if (it == _layerPaths.end()) return false;
    path = it->second;
    return true;
}

std::string TileManager::getLayerPath(TileCallback::LayerType id) const
{
    std::string path;
    tryGetLayerPath(id, path);
    return path;
}

bool TileManager::isHandlerExtension(const std::string& ext, std::string& suggested) const
{
    std::map<std::string, std::string>::const_iterator itr = _acceptHandlerExts.find(ext);
    if (itr != _acceptHandlerExts.end()) { suggested = itr->second; return true; } else return false;
}

osgDB::ReaderWriter* TileManager::getReaderWriter(const std::string& protocol, const std::string& url)
{
    std::lock_guard<std::mutex> _lock(_cachedRWMutex);
    std::map<std::string, osg::observer_ptr<osgDB::ReaderWriter>>::iterator it = _cachedReaderWriters.find(protocol);
    if (it != _cachedReaderWriters.end()) return it->second.get();
    std::string ext = osgDB::getFileExtension(url); it = _cachedReaderWriters.find(ext);
    if (it != _cachedReaderWriters.end()) return it->second.get();

    osgDB::ReaderWriter* rw = NULL;
#if OSG_MIN_VERSION_REQUIRED(3, 1, 5)
    if (!protocol.empty())
    {
        osgDB::Registry::ReaderWriterList rwList;
        osgDB::Registry::instance()->getReaderWriterListForProtocol(protocol, rwList);
        if (!rwList.empty()) { rw = rwList.front(); _cachedReaderWriters[protocol] = rw; }
    }
#endif
    if (!rw)
    {
        rw = osgDB::Registry::instance()->getReaderWriterForExtension(ext);
        if (rw) _cachedReaderWriters[ext] = rw;
    }
    // 诊断钩子(EARTH_IMG_DEBUG=1):记录协议/扩展名首次解析到哪个插件(瓦片 R/B 对调
    // 复发排查用,配合 FileCache/ReaderWriterWeb 同名钩子)。默认关闭。
    if (rw != NULL && getenv("EARTH_IMG_DEBUG") != NULL)
    {
        OSG_NOTICE << "[IMGDBG] TileManager protocol=" << protocol << " ext=" << ext
                   << " -> " << rw->className() << std::endl;
    }
    return rw;
}

bool TileManager::shouldMorph(TileCallback& cb) const
{ return _dynamicCallback.valid() ? _dynamicCallback->shouldMorph(cb) : false; }

void TileManager::updateTileGeometry(TileCallback& tileCB, osg::Geometry* geom)
{
    if (!_dynamicCallback || !geom) return;
    if (!_dynamicCallback->updateEntireTileGeometry(tileCB, geom))
    {
        osg::Vec3Array* va = static_cast<osg::Vec3Array*>(geom->getVertexArray());
        osg::Vec4Array* ca = static_cast<osg::Vec4Array*>(geom->getVertexAttribArray(GLOBE_ATTRIBUTE_INDEX));
        osg::Vec3d tileMin, tileMax; double tileWidth = 0.0, tileHeight;
        tileCB.computeTileExtent(tileMin, tileMax, tileWidth, tileHeight);

        unsigned int numRows = 16u, numCols = 16u;
        tileGeometryGridSize(geom, numRows, numCols);
        if (!va || va->size() < numRows * numCols) return;
        double invW = tileWidth / (float)(numCols - 1), invH = tileHeight / (float)(numRows - 1);
        const osg::Matrix& worldToLocal = tileCB.getTileWorldToLocalMatrix();
        for (unsigned int y = 0; y < numRows; ++y)
            for (unsigned int x = 0; x < numCols; ++x)
            {
                unsigned int vi = x + y * numCols; double altitude = 0.0;
                osg::Vec3d lla = tileCB.adjustLatitudeLongitudeAltitude(
                    tileMin + osg::Vec3d((double)x * invW, (double)y * invH, altitude), tileCB.getUseWebMercator());
                osg::Vec3 v0 = tileCB.convertToECEF(lla);

                osg::Vec3d ecef = _dynamicCallback->updateTileVertex(tileCB, lla[0], lla[1]);
                (*va)[vi] = osg::Vec3(ecef * worldToLocal);
                if (ca) (*ca)[vi] = osg::Vec4(v0 * worldToLocal, 0.0f);
            }
        if (!tileCB.getFlatten() && tileCB.getSkirtRatio() > 0.0f)
            tileCB.updateSkirtData(geom, osg::inDegrees(tileMax.y() - tileMin.y()), false);
        va->dirty(); if (ca) ca->dirty(); geom->dirtyBound();
    }
}
