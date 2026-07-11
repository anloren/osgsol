#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <cpl_conv.h>
#include <cpl_string.h>
#include <cpl_vsi.h>
#include <cpl_vsi_virtual.h>
#include <gdal.h>
#include <gdal_alg.h>
#include <gdal_frmts.h>
#include <gdal_priv.h>
#include <gdalwarper.h>
#include <ogr_srs_api.h>

namespace
{
    struct ActiveDrivers
    {
        std::vector<std::string> raster;
        std::vector<std::string> ogr;
    };

    [[noreturn]] void fail(const std::string& message)
    {
        std::cerr << "runtime probe failure: " << message << std::endl;
        std::exit(1);
    }

    void require(bool condition, const std::string& message)
    {
        if (!condition) fail(message);
    }

    std::vector<std::string> vsiPrefixes()
    {
        std::vector<std::string> result;
        char** prefixes = VSIGetFileSystemsPrefixes();
        for (int i = 0; prefixes && prefixes[i]; ++i) result.emplace_back(prefixes[i]);
        CSLDestroy(prefixes);
        std::sort(result.begin(), result.end());
        return result;
    }

    void printJsonArray(const std::vector<std::string>& values)
    {
        std::cout << '[';
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i) std::cout << ',';
            std::cout << '"' << values[i] << '"';
        }
        std::cout << ']';
    }

    std::vector<std::string> installScienceVfs()
    {
        VSIInstallCurlFileHandler();

        const std::set<std::string> localPrefixes = {
            "", "/vsicached/", "/vsicrypt/", "/vsigzip/", "/vsimem/",
            "/vsisparse/", "/vsistdin/", "/vsistdout/", "/vsisubfile/",
            "/vsitar/", "/vsizip/"
        };
        const std::string allowedRemote = "/vsicurl/";

        for (const std::string& prefix : vsiPrefixes())
        {
            if (prefix != allowedRemote && localPrefixes.count(prefix) == 0)
                VSIFileManager::RemoveHandler(prefix);
        }
        VSIInstallCurlFileHandler();

        std::vector<std::string> remote;
        for (const std::string& prefix : vsiPrefixes())
        {
            if (localPrefixes.count(prefix) == 0) remote.push_back(prefix);
        }
        require(remote == std::vector<std::string>{allowedRemote},
                "active remote VFS set is not exactly /vsicurl/");
        return remote;
    }

    ActiveDrivers registerScienceDrivers()
    {
        GDALRegister_GTiff();
        GDALRegister_VRT();
        GDALRegister_MEM();

        std::vector<std::string> drivers, rasterDrivers, ogrDrivers;
        GDALDriverManager* manager = GetGDALDriverManager();
        for (int i = 0; i < manager->GetDriverCount(); ++i)
        {
            GDALDriver* driver = manager->GetDriver(i);
            const std::string name = driver->GetDescription();
            drivers.push_back(name);
            const char* raster = driver->GetMetadataItem(GDAL_DCAP_RASTER);
            const char* vector = driver->GetMetadataItem(GDAL_DCAP_VECTOR);
            if (raster && CPLTestBool(raster))
                rasterDrivers.push_back(name);
            if (vector && CPLTestBool(vector))
                ogrDrivers.push_back(name);
        }
        std::sort(drivers.begin(), drivers.end());
        std::sort(rasterDrivers.begin(), rasterDrivers.end());
        std::sort(ogrDrivers.begin(), ogrDrivers.end());

        const std::vector<std::string> expected = {"GTiff", "MEM", "VRT"};
        require(drivers == expected, "active driver set is not exactly GTiff/MEM/VRT");
        require(rasterDrivers == expected,
                "active raster driver set is not exactly GTiff/MEM/VRT");
        require(GDALGetDriverByName("COG") == nullptr, "COG driver is active");
        require(GDALGetDriverByName("GNM") == nullptr, "GNM driver is active");
        return {rasterDrivers, ogrDrivers};
    }

    void setProjection(GDALDataset* dataset, int epsg)
    {
        OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
        require(srs != nullptr, "failed to allocate spatial reference");
        OSRSetAxisMappingStrategy(srs, OAMS_TRADITIONAL_GIS_ORDER);
        require(OSRImportFromEPSG(srs, epsg) == OGRERR_NONE, "EPSG import failed");
        char* wkt = nullptr;
        require(OSRExportToWkt(srs, &wkt) == OGRERR_NONE && wkt,
                "WKT export failed");
        require(dataset->SetProjection(wkt) == CE_None, "projection assignment failed");
        CPLFree(wkt);
        OSRDestroySpatialReference(srs);
    }

    void exerciseRuntime()
    {
        GDALDriver* memDriver = GetGDALDriverManager()->GetDriverByName("MEM");
        GDALDriver* gtiffDriver = GetGDALDriverManager()->GetDriverByName("GTiff");
        GDALDriver* vrtDriver = GetGDALDriverManager()->GetDriverByName("VRT");
        require(memDriver && gtiffDriver && vrtDriver, "required driver lookup failed");

        GDALDataset* source = memDriver->Create("", 8, 8, 1, GDT_Byte, nullptr);
        require(source != nullptr, "MEM creation failed");
        std::vector<unsigned char> sourcePixels(64);
        for (size_t i = 0; i < sourcePixels.size(); ++i)
            sourcePixels[i] = static_cast<unsigned char>(i + 1);
        require(source->GetRasterBand(1)->RasterIO(
                    GF_Write, 0, 0, 8, 8, sourcePixels.data(), 8, 8, GDT_Byte,
                    0, 0, nullptr) == CE_None,
                "MEM RasterIO failed");
        double sourceTransform[6] = {-1.0, 0.25, 0.0, 1.0, 0.0, -0.25};
        require(source->SetGeoTransform(sourceTransform) == CE_None,
                "source geotransform failed");
        setProjection(source, 4326);

        char** gtiffOptions = nullptr;
        gtiffOptions = CSLSetNameValue(gtiffOptions, "TILED", "YES");
        gtiffOptions = CSLAddString(gtiffOptions, "COMPRESS=ZSTD");
        GDALDataset* gtiff = gtiffDriver->CreateCopy(
            "/vsimem/science-runtime-probe.tif", source, false, gtiffOptions,
            nullptr, nullptr);
        CSLDestroy(gtiffOptions);
        require(gtiff != nullptr, "GTiff+ZSTD CreateCopy failed");
        GDALClose(gtiff);

        gtiff = static_cast<GDALDataset*>(GDALOpen(
            "/vsimem/science-runtime-probe.tif", GA_ReadOnly));
        require(gtiff != nullptr, "GTiff reopen failed");
        const char* compression =
            gtiff->GetMetadataItem("COMPRESSION", "IMAGE_STRUCTURE");
        require(compression && std::string(compression) == "ZSTD",
                "GTiff did not resolve ZSTD compression");

        GDALDataset* vrt = vrtDriver->CreateCopy(
            "/vsimem/science-runtime-probe.vrt", gtiff, false, nullptr,
            nullptr, nullptr);
        require(vrt != nullptr, "VRT CreateCopy failed");
        unsigned char vrtPixel = 0;
        require(vrt->GetRasterBand(1)->RasterIO(
                    GF_Read, 0, 0, 1, 1, &vrtPixel, 1, 1, GDT_Byte,
                    0, 0, nullptr) == CE_None && vrtPixel == 1,
                "VRT readback failed");
        GDALClose(vrt);
        GDALClose(gtiff);

        GDALDataset* warped = memDriver->Create("", 8, 8, 1, GDT_Byte, nullptr);
        require(warped != nullptr, "warp destination creation failed");
        double targetTransform[6] = {
            -111319.5, 27829.875, 0.0, 111325.0, 0.0, -27831.25
        };
        require(warped->SetGeoTransform(targetTransform) == CE_None,
                "target geotransform failed");
        setProjection(warped, 3857);
        require(GDALReprojectImage(source, source->GetProjectionRef(), warped,
                                   warped->GetProjectionRef(), GRA_NearestNeighbour,
                                   0.0, 0.0, nullptr, nullptr, nullptr) == CE_None,
                "GDALReprojectImage/PROJ failed");
        std::vector<unsigned char> warpedPixels(64);
        require(warped->GetRasterBand(1)->RasterIO(
                    GF_Read, 0, 0, 8, 8, warpedPixels.data(), 8, 8, GDT_Byte,
                    0, 0, nullptr) == CE_None,
                "warped RasterIO failed");
        require(std::any_of(warpedPixels.begin(), warpedPixels.end(),
                            [](unsigned char value) { return value != 0; }),
                "warp output is empty");

        GDALClose(warped);
        GDALClose(source);
        VSIUnlink("/vsimem/science-runtime-probe.vrt");
        VSIUnlink("/vsimem/science-runtime-probe.tif");
    }
}

int main()
{
    const ActiveDrivers drivers = registerScienceDrivers();
    const std::vector<std::string> remoteVfs = installScienceVfs();
    exerciseRuntime();
    const std::vector<std::string> allVfs = vsiPrefixes();

    std::cout << "{\n  \"schema_version\": 2,\n  \"gdal_version\": \""
              << GDALVersionInfo("RELEASE_NAME") << "\",\n"
              << "  \"active_raster_drivers\": ";
    printJsonArray(drivers.raster);
    std::cout << ",\n  \"active_ogr_drivers\": ";
    printJsonArray(drivers.ogr);
    std::cout << ",\n  \"active_vfs\": ";
    printJsonArray(allVfs);
    std::cout << ",\n  \"active_remote_vfs\": ";
    printJsonArray(remoteVfs);
    std::cout << ",\n  \"gtiff_zstd\": true,\n"
              << "  \"vrt_read\": true,\n  \"mem_rasterio\": true,\n"
              << "  \"warp_proj\": true,\n  \"cog_active\": false,\n"
              << "  \"gnm_active\": false\n}\n";

    GDALDestroyDriverManager();
    VSICleanupFileManager();
    return 0;
}
