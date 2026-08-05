#ifndef OSGSOL_EARTH_PROJECT_H
#define OSGSOL_EARTH_PROJECT_H

#include <cstddef>
#include <string>
#include <vector>

namespace earthproject
{
    constexpr int kCurrentSchemaVersion = 1;
    constexpr int kCurrentLayerDescriptorVersion = 2;
    constexpr std::size_t kMaxProjectJsonBytes = 16u * 1024u * 1024u;
    constexpr const char* kProjectSchema = "osgsol-earth-project";

    enum class TemporalMode
    {
        None,
        Discrete,
        Range,
        Live
    };

    struct Wgs84Bounds
    {
        bool valid = false;
        double westDeg = 0.0;
        double southDeg = 0.0;
        double eastDeg = 0.0;
        double northDeg = 0.0;
    };

    struct LayerSourceDescriptor
    {
        std::string kind;
        std::string uri;
        std::string providerId;
        std::string providerVersion;
        std::string crs = "EPSG:4326";
        Wgs84Bounds bounds;
        std::string attribution;
        std::string license;
    };

    struct LayerRenderDescriptor
    {
        std::string kind;
        bool visible = false;
        double opacity = 1.0;
        int order = 0;
        std::string styleId;
        std::string legendId;
    };

    struct TemporalBinding
    {
        TemporalMode mode = TemporalMode::None;
        std::string dimension;
        std::string granularity;
        std::string currentValue;
        std::vector<std::string> availableValues;
    };

    struct LayerCapabilities
    {
        bool temporal = false;
        bool identify = false;
        bool selectable = false;
        bool analysis = false;
        bool exportData = false;
    };

    // Serializable, renderer-independent record. It never owns an OSG node,
    // callback, API key, cookie, token, or provider runtime object.
    struct EarthLayerDescriptor
    {
        int descriptorVersion = kCurrentLayerDescriptorVersion;
        std::string id;
        std::string displayName;
        std::string group;
        LayerSourceDescriptor source;
        LayerRenderDescriptor render;
        TemporalBinding temporal;
        LayerCapabilities capabilities;
        std::string artifactId;
    };

    struct CameraState
    {
        double latitudeDeg = 0.0;
        double longitudeDeg = 0.0;
        double altitudeKm = 38119.0;
        double headingDeg = 0.0;
        double pitchDeg = -90.0;
        double rollDeg = 0.0;
    };

    struct WorkspaceState
    {
        std::string activeModule = "explore";
        bool drawerOpen = true;
        std::vector<std::string> activeArtifactIds;
    };

    struct EarthProject
    {
        int schemaVersion = kCurrentSchemaVersion;
        std::string projectId = "untitled-project";
        std::string name = "Untitled Earth Project";
        CameraState camera;
        std::vector<EarthLayerDescriptor> layers;
        WorkspaceState workspace;
    };

    struct ProjectDiagnostic
    {
        std::string code;
        std::string message;
    };

    struct ProjectLoadResult
    {
        bool ok = false;
        EarthProject project;
        int migratedFromVersion = -1;
        std::vector<ProjectDiagnostic> diagnostics;
        std::string errorCode;
        std::string errorMessage;
    };

    EarthProject normalizeProject(
        const EarthProject& project,
        std::vector<ProjectDiagnostic>* diagnostics = nullptr);

    std::string saveProjectJson(const EarthProject& project);
    ProjectLoadResult loadProjectJson(const std::string& json);
}

#endif
