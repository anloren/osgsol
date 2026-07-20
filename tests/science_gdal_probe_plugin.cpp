#include <cstdlib>

#include <cpl_vsi.h>
#include <gdal_frmts.h>
#include <gdal_priv.h>
#include <proj.h>
#include <zstd.h>

namespace
{
    __attribute__((constructor, used)) void initializeScienceG0Probe()
    {
        GDALRegister_GTiff();
        GDALRegister_VRT();
        GDALRegister_MEM();
        VSIInstallCurlFileHandler();

        PJ_CONTEXT* context = proj_context_create();
        if (!context || ZSTD_versionNumber() == 0 ||
            GetGDALDriverManager()->GetDriverByName("GTiff") == nullptr)
            std::exit(EXIT_FAILURE);
        proj_context_destroy(context);
    }
}

extern "C" __attribute__((visibility("default"), used))
int osgsol_science_g0_probe_anchor()
{
    return static_cast<int>(ZSTD_versionNumber() != 0);
}
