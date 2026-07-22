#include "science_plugin_api.h"

#include "LayerManager.h"
#include "science_ai_tools.h"
#include "science_earth_panel.h"
#include "science_preview_layer.h"
#include "science_query_builder.h"
#include "science_workbench_model.h"
#include "science_workbench_protocol.h"

#include <AlphaEarthProvider.h>
#include <CopernicusDemProvider.h>
#include <Era5AgroProvider.h>
#include <ScienceQueryService.h>
#include <ScienceSourceRegistry.h>
#include <Sentinel2Provider.h>

#include <osg/Notify>
#include <ui/ImGuiComponents.h>

#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
            error.clear();
            if (!registry->add(
                    std::unique_ptr<earthscience::IScienceProvider>(
                        new earthscience::Era5AgroProvider(
                            earthscience::Era5AgroProduct::LandSurface)),
                    error))
                OSG_WARN << "ScienceEarth ERA5-Land registration failed: "
                         << error << std::endl;
            error.clear();
            if (!registry->add(
                    std::unique_ptr<earthscience::IScienceProvider>(
                        new earthscience::Era5AgroProvider(
                            earthscience::Era5AgroProduct::AgriculturalClimate)),
                    error))
                OSG_WARN << "ScienceEarth ERA5 registration failed: "
                         << error << std::endl;

            service.reset(new earthscience::ScienceQueryService(
                std::move(registry)));
            layer = new SciencePreviewLayer(service.get());

            std::string workbenchError;
            ScienceWorkbenchAction source;
            source.kind = ScienceWorkbenchActionKind::SelectSource;
            source.sourceId = "era5-agricultural-climate";
            workbench.dispatch(source, workbenchError);
            ScienceWorkbenchAction analysis;
            analysis.kind = ScienceWorkbenchActionKind::SelectAnalysis;
            analysis.analysisId = "annual-agricultural-climate";
            workbench.dispatch(analysis, workbenchError);
            ScienceWorkbenchAction years;
            years.kind = ScienceWorkbenchActionKind::SetYearRange;
            years.firstYear = 2017;
            years.lastYear = 2025;
            workbench.dispatch(years, workbenchError);
        }

        // Declaration order is deliberate: destruction runs panel, layer,
        // then service so the preview never observes a destroyed service.
        std::unique_ptr<earthscience::ScienceQueryService> service;
        osg::ref_ptr<SciencePreviewLayer> layer;
        ScienceEarthPanel panel;
        ScienceWorkbenchModel workbench;
        std::uint64_t workbenchJobId = 0;
        std::uint64_t lastSyncedJobId =
            std::numeric_limits<std::uint64_t>::max();
        earthscience::ScienceJobState lastSyncedState =
            earthscience::ScienceJobState::Unavailable;
        earthscience::ScienceProgressStage lastSyncedStage =
            earthscience::ScienceProgressStage::Idle;
        std::uint64_t lastSyncedCompleted =
            std::numeric_limits<std::uint64_t>::max();
        std::uint64_t lastSyncedTotal =
            std::numeric_limits<std::uint64_t>::max();
        std::string lastSyncedMessage;
        std::string lastSyncedArtifactId;
    };

    SciencePluginSession* session(void* value)
    {
        return static_cast<SciencePluginSession*>(value);
    }

    const earthscience::ScienceSourceDescriptor* findSource(
        const std::vector<earthscience::ScienceSourceDescriptor>& sources,
        const std::string& sourceId)
    {
        for (const earthscience::ScienceSourceDescriptor& source : sources)
            if (source.id == sourceId) return &source;
        return nullptr;
    }

    void syncWorkbench(SciencePluginSession& runtime)
    {
        const earthscience::ScienceJobSnapshot snapshot =
            runtime.service->snapshot();
        const std::string artifactId = snapshot.displayArtifact
            ? snapshot.displayArtifact->artifactId : std::string();
        const bool changed = snapshot.jobId != runtime.lastSyncedJobId ||
            snapshot.state != runtime.lastSyncedState ||
            snapshot.progress.stage != runtime.lastSyncedStage ||
            snapshot.progress.completedUnits != runtime.lastSyncedCompleted ||
            snapshot.progress.totalUnits != runtime.lastSyncedTotal ||
            snapshot.message != runtime.lastSyncedMessage ||
            artifactId != runtime.lastSyncedArtifactId;
        if (!changed) return;
        runtime.workbench.applyJobSnapshot(snapshot);
        runtime.lastSyncedJobId = snapshot.jobId;
        runtime.lastSyncedState = snapshot.state;
        runtime.lastSyncedStage = snapshot.progress.stage;
        runtime.lastSyncedCompleted = snapshot.progress.completedUnits;
        runtime.lastSyncedTotal = snapshot.progress.totalUnits;
        runtime.lastSyncedMessage = snapshot.message;
        runtime.lastSyncedArtifactId = artifactId;
    }

    bool prepareWorkbenchQuery(SciencePluginSession& runtime,
                               std::string& error)
    {
        const ScienceWorkbenchViewModel& view =
            runtime.workbench.viewModel();
        const std::vector<earthscience::ScienceSourceDescriptor> sources =
            runtime.service->listSources();
        const earthscience::ScienceSourceDescriptor* source =
            findSource(sources, view.draft.sourceId);
        if (!source)
        {
            error = "workbench-source-not-found";
            return false;
        }
        if (!view.target.locked ||
            view.target.requested.kind !=
                earthscience::ScienceGeometryKind::Point)
        {
            error = "workbench-point-target-required";
            return false;
        }
        if (view.draft.time.explicitYears.empty())
        {
            error = "workbench-time-required";
            return false;
        }
        const int firstYear = view.draft.time.explicitYears.front();
        const int lastYear = view.draft.time.explicitYears.back();
        earthscience::GeoTemporalQuery query;
        if (source->id == "era5-land-surface-history" ||
            source->id == "era5-agricultural-climate")
        {
            query = makeScienceVariablePointSeriesQuery(
                *source, view.target.requested.point.latitude,
                view.target.requested.point.longitude,
                firstYear, lastYear);
        }
        else if (source->id == "alphaearth-foundations")
        {
            query = makeSciencePointSeriesQuery(
                *source, view.target.requested.point.latitude,
                view.target.requested.point.longitude,
                firstYear, lastYear);
        }
        else
        {
            error = "workbench-analysis-not-supported-for-source";
            return false;
        }
        // The product workbench always exposes the exact cost immediately
        // above its sticky Run action. Clicking Run is the explicit consent
        // for that disclosed request; there is no hidden second checkbox.
        query.analysis.confirmedLargeRequest = true;
        if (!runtime.service->validateQuery(query, error)) return false;
        const earthscience::ScienceQueryCost cost =
            runtime.service->estimate(query);
        runtime.workbench.configureDraft(query);
        runtime.workbench.applyCost(cost);
        if (cost.requiresConfirmation &&
            !query.analysis.confirmedLargeRequest)
        {
            error = "workbench-cost-confirmation-required";
            return false;
        }
        return true;
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

    bool copyWorkbenchSnapshot(void* value,
                               OsgSolScienceUiBufferV1* output)
    {
        SciencePluginSession* runtime = session(value);
        if (!runtime || !output) return false;
        syncWorkbench(*runtime);
        const ScienceWorkbenchViewModel& view =
            runtime->workbench.viewModel();
        output->revision = view.revision;
        const std::string snapshot = serializeScienceWorkbenchSnapshot(
            view, runtime->service->listSources(),
            runtime->workbench.artifact(view.activeArtifactId));
        return copyScienceWorkbenchSnapshot(snapshot, output);
    }

    bool dispatchWorkbenchAction(void* value, const char* actionUtf8,
                                 std::size_t actionSize, char* error,
                                 std::size_t errorSize)
    {
        SciencePluginSession* runtime = session(value);
        if (!runtime)
        {
            copyError(error, errorSize, "workbench-session-unavailable");
            return false;
        }
        syncWorkbench(*runtime);
        ScienceWorkbenchAction action;
        std::string detail;
        if (!parseScienceWorkbenchAction(
                actionUtf8, actionSize, action, detail))
        {
            copyError(error, errorSize, detail);
            return false;
        }
        if (action.kind == ScienceWorkbenchActionKind::Run)
        {
            if (!prepareWorkbenchQuery(*runtime, detail) ||
                !runtime->workbench.dispatch(action, detail))
            {
                copyError(error, errorSize, detail);
                return false;
            }
            std::optional<earthscience::GeoTemporalQuery> query =
                runtime->workbench.takePendingSubmission();
            if (!query)
            {
                copyError(error, errorSize,
                          "workbench-submission-unavailable");
                return false;
            }
            runtime->workbenchJobId = runtime->service->submit(*query);
            if (runtime->workbenchJobId == 0)
            {
                copyError(error, errorSize, "workbench-submit-failed");
                return false;
            }
            syncWorkbench(*runtime);
            return true;
        }
        if (action.kind == ScienceWorkbenchActionKind::Cancel)
        {
            if (!runtime->workbench.dispatch(action, detail))
            {
                copyError(error, errorSize, detail);
                return false;
            }
            if (runtime->workbenchJobId != 0)
                runtime->service->cancel(runtime->workbenchJobId);
            syncWorkbench(*runtime);
            return true;
        }
        if (action.kind == ScienceWorkbenchActionKind::OpenReport &&
            !runtime->service->showArtifact(action.artifactId))
        {
            copyError(error, errorSize, "workbench-artifact-not-found");
            return false;
        }
        if (!runtime->workbench.dispatch(action, detail))
        {
            copyError(error, errorSize, detail);
            return false;
        }
        return true;
    }

    const OsgSolSciencePluginApiV4 pluginApi = {
        {
            OSGSOL_SCIENCE_PLUGIN_ABI_V4,
            sizeof(OsgSolSciencePluginApiV4),
            createSession,
            destroySession,
            sceneNode,
            setVisible,
            bindGeoRaster,
            registerAiTools,
            bindGui,
            drawOperations,
            drawResults,
        },
        copyWorkbenchSnapshot,
        dispatchWorkbenchAction,
    };
}

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV3* osgsol_science_g0_probe_anchor()
{
    return &pluginApi.v3;
}
