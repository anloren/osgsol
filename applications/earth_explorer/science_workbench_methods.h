#ifndef OSGSOL_SCIENCE_WORKBENCH_METHODS_H
#define OSGSOL_SCIENCE_WORKBENCH_METHODS_H

#include <string>
#include <vector>

struct ScienceWorkbenchMethod
{
    std::string id;
    std::string label;
    std::string summary;
    std::string runLabel;
};

inline bool isScienceWorkbenchEra5Source(const std::string& sourceId)
{
    return sourceId == "era5-land-surface-history" ||
        sourceId == "era5-agricultural-climate";
}

inline std::vector<ScienceWorkbenchMethod>
scienceWorkbenchMethodsForSourceId(
    const std::string& sourceId, bool timeSeriesOutput,
    bool analysisOutput, bool rasterLayerOutput)
{
    if (sourceId == "alphaearth-foundations")
    {
        return {
            {"annual-summary", "年度变化曲线",
             "比较 64 维地表表征在各年份相对首年的方向与距离变化。",
             "分析年度变化"},
            {"change-map", "区域变化热图",
             "比较首年与末年，在固定区域内定位变化较强的相对热点。",
             "生成区域变化热图"},
            {"direction-change", "高维方向与距离",
             "同时计算点积、余弦相似度、余弦距离、欧氏距离和角距离。",
             "分析高维方向与距离"},
        };
    }
    if (isScienceWorkbenchEra5Source(sourceId))
    {
        return {
            {"annual-summary", "年度统计与趋势",
             "按变量各自的科学单位呈现年度均值或年度总量，不混合坐标轴。",
             sourceId == "era5-agricultural-climate"
                 ? "生成年度农业气候剖面"
                 : "生成年度地表与土壤剖面"},
        };
    }
    if (sourceId == "sentinel-2-l2a")
    {
        return {
            {"satellite-preview", "自然色卫星影像",
             "在所选年份内寻找云量较低的 Sentinel-2 L2A 真彩场景。",
             "加载 Sentinel-2 真彩影像"},
        };
    }
    if (sourceId == "copernicus-dem-glo-30")
    {
        return {
            {"terrain-preview", "地表高程",
             "加载 Copernicus DEM GLO-30 数字表面模型，并按高程着色。",
             "加载地表高程"},
        };
    }

    std::vector<ScienceWorkbenchMethod> methods;
    if (timeSeriesOutput)
        methods.push_back(
            {"annual-summary", "年度统计与趋势",
             "按年份生成该数据源支持的时间序列。", "开始年度分析"});
    if (analysisOutput)
        methods.push_back(
            {"change-map", "区域变化热图",
             "比较首年与末年的空间变化。", "生成区域变化热图"});
    if (methods.empty() && rasterLayerOutput)
        methods.push_back(
            {"raster-preview", "空间图层",
             "加载该数据源支持的空间图层。", "加载空间图层"});
    return methods;
}

#endif
