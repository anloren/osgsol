#ifndef OSGSOL_SCIENCE_REMOTE_OPEN_H
#define OSGSOL_SCIENCE_REMOTE_OPEN_H

#include <mutex>
#include <string>

#include <cpl_conv.h>
#include <cpl_string.h>
#include <gdal_frmts.h>

namespace earthscience
{
namespace remoteopen
{
    struct OperationBudget
    {
        int connectSeconds = 0;
        int totalSeconds = 0;
    };

    inline OperationBudget metadataBudget()
    {
        return {8, 15};
    }

    inline OperationBudget rasterBudget()
    {
        return {8, 25};
    }

    inline char** appendBudget(char** options, OperationBudget budget)
    {
        const std::string connect = std::to_string(budget.connectSeconds);
        const std::string total = std::to_string(budget.totalSeconds);
        options = CSLSetNameValue(options, "CONNECTTIMEOUT", connect.c_str());
        return CSLSetNameValue(options, "TIMEOUT", total.c_str());
    }

    inline void configureRasterIo()
    {
        static std::once_flag registration;
        std::call_once(registration, []()
        {
            GDALRegister_GTiff();
            GDALRegister_VRT();
            GDALRegister_MEM();
            CPLSetConfigOption("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR");
            CPLSetConfigOption(
                "CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".tif,.tiff");
            CPLSetConfigOption("GDAL_HTTP_VERSION", "2TLS");
            CPLSetConfigOption("GDAL_HTTP_MULTIPLEX", "YES");
            CPLSetConfigOption(
                "GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES");
            const OperationBudget budget = rasterBudget();
            const std::string connect = std::to_string(budget.connectSeconds);
            const std::string total = std::to_string(budget.totalSeconds);
            CPLSetConfigOption("GDAL_HTTP_CONNECTTIMEOUT", connect.c_str());
            CPLSetConfigOption("GDAL_HTTP_TIMEOUT", total.c_str());
        });
    }
}
}

#endif
