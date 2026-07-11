#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cpl_conv.h>
#include <cpl_string.h>
#include <gdal_frmts.h>
#include <gdal_priv.h>

namespace
{
    constexpr int FIXTURE_SIZE = 256;
    constexpr int BAND_COUNT = 64;
    constexpr int NODATA_VALUE = -128;

    [[noreturn]] void fail(const std::string& message)
    {
        std::cerr << "ScienceGdalSpike failure: " << message << std::endl;
        std::exit(1);
    }

    void require(bool condition, const std::string& message)
    {
        if (!condition) fail(message);
    }

    std::string bandName(int zeroBasedBand)
    {
        std::ostringstream stream;
        stream << 'A' << std::setw(2) << std::setfill('0') << zeroBasedBand;
        return stream.str();
    }

    double dequantize(std::int8_t value)
    {
        const double magnitude = std::abs(static_cast<double>(value)) / 127.5;
        return std::copysign(magnitude * magnitude, static_cast<double>(value));
    }

    void registerScienceDrivers()
    {
        GDALRegister_GTiff();
        GDALRegister_VRT();
        GDALRegister_MEM();

        std::vector<std::string> active;
        GDALDriverManager* manager = GetGDALDriverManager();
        for (int i = 0; i < manager->GetDriverCount(); ++i)
            active.emplace_back(manager->GetDriver(i)->GetDescription());
        std::sort(active.begin(), active.end());
        require(active == std::vector<std::string>({"GTiff", "MEM", "VRT"}),
                "manual registration exposed drivers outside GTiff/VRT/MEM");
        require(manager->GetDriverByName("COG") == nullptr,
                "unrelated COG driver must remain unregistered");
        require(manager->GetDriverByName("PNG") == nullptr,
                "unrelated PNG driver must remain unregistered");
    }

    std::vector<std::int8_t> fixtureBand(int zeroBasedBand)
    {
        std::vector<std::int8_t> values(FIXTURE_SIZE * FIXTURE_SIZE);
        for (int y = 0; y < FIXTURE_SIZE; ++y)
        {
            for (int x = 0; x < FIXTURE_SIZE; ++x)
            {
                int value = ((zeroBasedBand * 11 + x * 3 + y * 5) % 255) - 127;
                if (zeroBasedBand == 1 && x == 20)
                    value = (y == 0) ? -127 : y - 128;
                values[y * FIXTURE_SIZE + x] = static_cast<std::int8_t>(value);
            }
        }

        if (zeroBasedBand == 1)
        {
            values[10 * FIXTURE_SIZE + 10] = 64;
            values[10 * FIXTURE_SIZE + 11] = NODATA_VALUE;
            values[10 * FIXTURE_SIZE + 12] = 0;
        }
        else if (zeroBasedBand == 16)
            values[10 * FIXTURE_SIZE + 10] = -64;
        else if (zeroBasedBand == 9)
            values[10 * FIXTURE_SIZE + 10] = 127;
        return values;
    }

    void createFixture(const std::filesystem::path& path)
    {
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        require(driver != nullptr, "GTiff driver lookup failed");

        char** options = nullptr;
        options = CSLSetNameValue(options, "TILED", "YES");
        options = CSLSetNameValue(options, "BLOCKXSIZE", "64");
        options = CSLSetNameValue(options, "BLOCKYSIZE", "64");
        options = CSLSetNameValue(options, "COMPRESS", "ZSTD");
        options = CSLSetNameValue(options, "INTERLEAVE", "BAND");
        options = CSLSetNameValue(options, "BIGTIFF", "YES");
        GDALDataset* dataset = driver->Create(
            path.string().c_str(), FIXTURE_SIZE, FIXTURE_SIZE, BAND_COUNT, GDT_Int8, options);
        CSLDestroy(options);
        require(dataset != nullptr, "failed to create the signed-int8 ZSTD fixture");

        const double southUpTransform[6] = {100.0, 1.0, 0.0, 200.0, 0.0, 1.0};
        require(dataset->SetGeoTransform(const_cast<double*>(southUpTransform)) == CE_None,
                "failed to assign the fixture geotransform");

        for (int zeroBasedBand = 0; zeroBasedBand < BAND_COUNT; ++zeroBasedBand)
        {
            GDALRasterBand* band = dataset->GetRasterBand(zeroBasedBand + 1);
            require(band != nullptr, "fixture band lookup failed");
            band->SetDescription(bandName(zeroBasedBand).c_str());
            require(band->SetNoDataValue(NODATA_VALUE) == CE_None,
                    "failed to assign the NoData sentinel");
            std::vector<std::int8_t> values = fixtureBand(zeroBasedBand);
            require(band->RasterIO(GF_Write, 0, 0, FIXTURE_SIZE, FIXTURE_SIZE,
                                   values.data(), FIXTURE_SIZE, FIXTURE_SIZE, GDT_Int8,
                                   0, 0, nullptr) == CE_None,
                    "failed to write a fixture band");
        }

        int overviewFactors[] = {2, 4};
        require(dataset->BuildOverviews("NEAREST", 2, overviewFactors, 0, nullptr,
                                        nullptr, nullptr) == CE_None,
                "failed to build the fixture overview pyramid");
        GDALClose(dataset);
    }

    std::string xmlEscape(const std::string& value)
    {
        std::string escaped;
        for (char character : value)
        {
            if (character == '&') escaped += "&amp;";
            else if (character == '<') escaped += "&lt;";
            else if (character == '>') escaped += "&gt;";
            else if (character == '\"') escaped += "&quot;";
            else escaped += character;
        }
        return escaped;
    }

    void createTrustedVerticalFlipVrt(const std::filesystem::path& source,
                                      const std::filesystem::path& output)
    {
        std::ofstream stream(output);
        require(stream.good(), "failed to create the trusted VRT");
        stream << "<VRTDataset rasterXSize=\"" << FIXTURE_SIZE
               << "\" rasterYSize=\"" << FIXTURE_SIZE << "\">\n"
               << "  <GeoTransform>100,1,0,456,0,-1</GeoTransform>\n";
        const std::string escapedSource = xmlEscape(source.string());
        for (int band = 1; band <= BAND_COUNT; ++band)
        {
            stream << "  <VRTRasterBand dataType=\"Int8\" band=\"" << band << "\">\n"
                   << "    <Description>" << bandName(band - 1) << "</Description>\n"
                   << "    <NoDataValue>" << NODATA_VALUE << "</NoDataValue>\n";
            for (int sourceY = 0; sourceY < FIXTURE_SIZE; ++sourceY)
            {
                stream << "    <SimpleSource>\n"
                       << "      <SourceFilename relativeToVRT=\"0\">" << escapedSource
                       << "</SourceFilename>\n"
                       << "      <SourceBand>" << band << "</SourceBand>\n"
                       << "      <SrcRect xOff=\"0\" yOff=\"" << sourceY
                       << "\" xSize=\"" << FIXTURE_SIZE << "\" ySize=\"1\"/>\n"
                       << "      <DstRect xOff=\"0\" yOff=\""
                       << (FIXTURE_SIZE - 1 - sourceY) << "\" xSize=\"" << FIXTURE_SIZE
                       << "\" ySize=\"1\"/>\n"
                       << "    </SimpleSource>\n";
            }
            stream << "  </VRTRasterBand>\n";
        }
        stream << "</VRTDataset>\n";
        require(stream.good(), "failed to finish the trusted VRT");
    }

    std::int8_t readInt8(GDALRasterBand* band, int x, int y)
    {
        std::int8_t value = 0;
        require(band->RasterIO(GF_Read, x, y, 1, 1, &value, 1, 1, GDT_Int8,
                               0, 0, nullptr) == CE_None,
                "single-pixel read failed");
        return value;
    }

    void verifyRgbAndMask(GDALDataset* dataset)
    {
        const int rgbBands[] = {2, 17, 10};
        const std::string expectedNames[] = {"A01", "A16", "A09"};
        const std::int8_t expectedValues[] = {64, -64, 127};
        for (int channel = 0; channel < 3; ++channel)
        {
            GDALRasterBand* band = dataset->GetRasterBand(rgbBands[channel]);
            require(band && std::string(band->GetDescription()) == expectedNames[channel],
                    "RGB band order is not A01/A16/A09");
            const std::int8_t raw = readInt8(band, 10, 10);
            require(raw == expectedValues[channel], "RGB fixture value changed");
            const double expected = std::copysign(
                std::pow(std::abs(static_cast<double>(raw)) / 127.5, 2.0),
                static_cast<double>(raw));
            require(std::abs(dequantize(raw) - expected) < 1e-15,
                    "signed AlphaEarth dequantization is not exact");
        }

        GDALRasterBand* a01 = dataset->GetRasterBand(2);
        int hasNoData = 0;
        require(a01->GetNoDataValue(&hasNoData) == NODATA_VALUE && hasNoData,
                "NoData sentinel was not preserved");
        require(readInt8(a01, 11, 10) == NODATA_VALUE,
                "NoData was incorrectly converted to zero");
        require(readInt8(a01, 12, 10) == 0,
                "valid zero sample was not preserved");
        GDALRasterBand* mask = a01->GetMaskBand();
        require(mask != nullptr && (a01->GetMaskFlags() & GMF_NODATA) != 0,
                "NoData did not produce a GDAL mask band");
        unsigned char maskValues[2] = {};
        require(mask->RasterIO(GF_Read, 11, 10, 2, 1, maskValues, 2, 1, GDT_Byte,
                               0, 0, nullptr) == CE_None,
                "NoData mask read failed");
        require(maskValues[0] == 0 && maskValues[1] == 255,
                "NoData and valid zero are not distinguished by the mask");
    }

    void verifyOrientation(GDALDataset* vrt)
    {
        GDALRasterBand* a01 = vrt->GetRasterBand(2);
        require(readInt8(a01, 20, 0) == 127,
                "trusted VRT top row does not map from the source bottom row");
        require(readInt8(a01, 20, FIXTURE_SIZE - 1) == -127,
                "trusted VRT bottom row does not map from the source top row");
        double transform[6] = {};
        require(vrt->GetGeoTransform(transform) == CE_None && transform[5] == -1.0,
                "trusted VRT is not top-down in geographic coordinates");
        const double topCenterY = transform[3] + 0.5 * transform[5];
        const double bottomCenterY = transform[3] + (FIXTURE_SIZE - 0.5) * transform[5];
        require(topCenterY == 455.5 && bottomCenterY == 200.5,
                "trusted VRT coordinates do not match the known top-down extent");
    }

    void verifyBoundedOverviewWindow(GDALDataset* dataset)
    {
        GDALRasterBand* a01 = dataset->GetRasterBand(2);
        require(a01->GetOverviewCount() == 2, "fixture overview pyramid is incomplete");
        GDALRasterBand* overview = a01->GetOverview(1);
        require(overview && overview->GetXSize() == 64 && overview->GetYSize() == 64,
                "4x overview was not selected for the bbox read");

        const int sourceX = 64, sourceY = 64, sourceSize = 64;
        const int overviewFactor = FIXTURE_SIZE / overview->GetXSize();
        const int readX = sourceX / overviewFactor;
        const int readY = sourceY / overviewFactor;
        const int readSize = sourceSize / overviewFactor;
        require(readSize * readSize < FIXTURE_SIZE * FIXTURE_SIZE,
                "bbox plan unexpectedly covers the full raster");
        std::vector<std::int8_t> window(readSize * readSize);
        require(overview->RasterIO(GF_Read, readX, readY, readSize, readSize,
                                   window.data(), readSize, readSize, GDT_Int8,
                                   0, 0, nullptr) == CE_None,
                "bounded overview/window read failed");
        require(!window.empty(), "bounded overview/window read returned no samples");
    }
}

int main()
{
    registerScienceDrivers();
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("osgsol-science-gdal-spike-" + std::to_string(std::rand()));
    std::filesystem::create_directories(root);
    const std::filesystem::path cogPath = root / "alphaearth-mini.tif";
    const std::filesystem::path vrtPath = root / "alphaearth-mini-flip.vrt";

    createFixture(cogPath);
    createTrustedVerticalFlipVrt(cogPath, vrtPath);

    GDALDataset* cog = static_cast<GDALDataset*>(
        GDALOpenEx(cogPath.string().c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    require(cog != nullptr, "failed to reopen the synthetic COG-style fixture");
    require(std::string(cog->GetDriverName()) == "GTiff", "fixture did not reopen as GTiff");
    require(cog->GetRasterCount() == BAND_COUNT &&
            cog->GetRasterBand(1)->GetRasterDataType() == GDT_Int8,
            "fixture is not 64-band signed int8");
    require(std::string(cog->GetMetadataItem("COMPRESSION", "IMAGE_STRUCTURE")) == "ZSTD",
            "fixture compression is not ZSTD");
    verifyRgbAndMask(cog);
    verifyBoundedOverviewWindow(cog);

    GDALDataset* vrt = static_cast<GDALDataset*>(
        GDALOpenEx(vrtPath.string().c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    require(vrt != nullptr && std::string(vrt->GetDriverName()) == "VRT",
            "trusted vertical-flip VRT did not open");
    verifyOrientation(vrt);

    GDALClose(vrt);
    GDALClose(cog);
    std::filesystem::remove_all(root);
    std::cout << "ScienceGdalSpike: 64-band Int8/ZSTD, RGB, mask, orientation, and "
                 "overview window verified" << std::endl;
    return 0;
}
