#include "science_plugin_api.h"

#include "LayerManager.h"
#include "science_ai_tools.h"
#include "science_earth_panel.h"
#include "science_preview_layer.h"

#include <AlphaEarthProvider.h>
#include <CopernicusDemProvider.h>
#include <ScienceQueryService.h>
#include <ScienceSourceRegistry.h>
#include <Sentinel2Provider.h>

#include <osg/Notify>
#include <ui/ImGuiComponents.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <string>

namespace
{
    void copyError(char* output, std::size_t outputSize,
                   const std::string& message)
    {
        if (output && outputSize > 0)
            std::snprintf(output, outputSize, "%s", message.c_str());
    }

    class SciencePluginSession
    {
    public:
        explicit SciencePluginSession(const std::string& indexPath)
        {
            std::unique_ptr<earthscience::ScienceSourceRegistry> registry(
                new earthscience::ScienceSourceRegistry);
            std::string error;
            if (!registry->add(
                    std::unique_ptr<earthscience::IScienceProvider>(
                        new earthscience::AlphaEarthProvider(indexPath)),
                    error))
                OSG_WARN << "ScienceEarth provider registration failed: "
                         << error << std::endl;
            error.clear();
            if (!registry->add(
                    std::unique_ptr<earthscience::IScienceProvider>(
                        new earthscience::Sentinel2Provider), error))
                OSG_WARN << "ScienceEarth Sentinel-2 registration failed: "
                         << error << std::endl;
            error.clear();
            if (!registry->add(
                    std::unique_ptr<earthscience::IScienceProvider>(
                        new earthscience::CopernicusDemProvider), error))
                OSG_WARN << "ScienceEarth Copernicus DEM registration failed: "
                         << error << std::endl;

            service.reset(new earthscience::ScienceQueryService(
                std::move(registry)));
            layer = new SciencePreviewLayer(service.get());
        }

        // Declaration order is deliberate: destruction runs panel, layer,
        // then service so the preview never observes a destroyed service.
        std::unique_ptr<earthscience::ScienceQueryService> service;
        osg::ref_ptr<SciencePreviewLayer> layer;
        ScienceEarthPanel panel;
    };

    SciencePluginSession* session(void* value)
    {
        return static_cast<SciencePluginSession*>(value);
    }

    void* createSession(const char* indexPath, char* error,
                        std::size_t errorSize)
    {
        try
        {
            if (!indexPath || !*indexPath)
            {
                copyError(error, errorSize,
                          "ScienceEarth AlphaEarth index path is empty");
                return nullptr;
            }
            return new SciencePluginSession(indexPath);
        }
        catch (const std::exception& exception)
        {
            copyError(error, errorSize, exception.what());
        }
        catch (...)
        {
            copyError(error, errorSize,
                      "ScienceEarth plugin initialization failed");
        }
        return nullptr;
    }

    void destroySession(void* value)
    {
        delete session(value);
    }

    osg::Node* sceneNode(void* value)
    {
        SciencePluginSession* runtime = session(value);
        return runtime ? runtime->layer.get() : nullptr;
    }

    void setVisible(void* value, bool visible)
    {
        SciencePluginSession* runtime = session(value);
        if (runtime) runtime->layer->setVisible(visible);
    }

    void bindGeoRaster(void* value,
                       const OsgSolGeoRasterBridgeV1* bridge)
    {
        SciencePluginSession* runtime = session(value);
        if (runtime) runtime->layer->bindGeoRaster(bridge);
    }

    void registerAiTools(void* value, earthai::ToolRegistry* tools,
                         LayerManager* layers,
                         osgVerse::EarthManipulator* manipulator)
    {
        SciencePluginSession* runtime = session(value);
        if (!runtime) return;
        registerScienceResearchTools(tools, runtime->service.get(),
                                     runtime->layer.get(), layers, manipulator);
    }

    void bindGui(void*, const OsgSolScienceGuiBridgeV1* bridge)
    {
        if (!bridge || !bridge->context || !bridge->allocate ||
            !bridge->deallocate) return;
        ImGui::SetAllocatorFunctions(
            bridge->allocate, bridge->deallocate,
            bridge->allocatorUserData);
        ImGui::SetCurrentContext(
            static_cast<ImGuiContext*>(bridge->context));
    }

    void drawOperations(void* value, LayerManager* layers,
                        osgVerse::EarthManipulator* manipulator)
    {
        SciencePluginSession* runtime = session(value);
        if (!runtime) return;
        runtime->panel.drawOperations(runtime->service.get(),
                                      runtime->layer.get(), layers, manipulator);
    }

    void drawResults(void* value, LayerManager* layers)
    {
        SciencePluginSession* runtime = session(value);
        if (!runtime) return;
        runtime->panel.drawResults(runtime->service.get(),
                                   runtime->layer.get(), layers);
    }

    const OsgSolSciencePluginApiV3 pluginApi = {
        OSGSOL_SCIENCE_PLUGIN_ABI_V3,
        sizeof(OsgSolSciencePluginApiV3),
        createSession,
        destroySession,
        sceneNode,
        setVisible,
        bindGeoRaster,
        registerAiTools,
        bindGui,
        drawOperations,
        drawResults,
    };
}

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV3* osgsol_science_g0_probe_anchor()
{
    return &pluginApi;
}
