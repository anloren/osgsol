#include "earth_project.h"

#include <picojson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>

namespace earthproject
{
    namespace
    {
        using Object = picojson::object;
        using Array = picojson::array;

        void diagnose(std::vector<ProjectDiagnostic>* diagnostics,
                      const std::string& code, const std::string& message)
        {
            if (!diagnostics) return;
            ProjectDiagnostic value;
            value.code = code;
            value.message = message;
            diagnostics->push_back(value);
        }

        double finiteOr(double value, double fallback,
                        std::vector<ProjectDiagnostic>* diagnostics,
                        const char* field)
        {
            if (std::isfinite(value)) return value;
            diagnose(diagnostics, "non_finite_number",
                     std::string(field) + " was reset to a finite default");
            return fallback;
        }

        double clampNumber(double value, double minimum, double maximum,
                           std::vector<ProjectDiagnostic>* diagnostics,
                           const char* field)
        {
            const double clamped = std::max(minimum, std::min(maximum, value));
            if (clamped != value)
                diagnose(diagnostics, "number_clamped",
                         std::string(field) + " was outside its valid range");
            return clamped;
        }

        double normalizeLongitude(double value)
        {
            double output = std::fmod(value + 180.0, 360.0);
            if (output < 0.0) output += 360.0;
            return output - 180.0;
        }

        double normalizeHeading(double value)
        {
            double output = std::fmod(value, 360.0);
            if (output < 0.0) output += 360.0;
            return output;
        }

        double normalizeSignedAngle(double value)
        {
            double output = std::fmod(value + 180.0, 360.0);
            if (output < 0.0) output += 360.0;
            return output - 180.0;
        }

        int hexDigit(char value)
        {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        }

        std::string normalizedQueryKey(const std::string& value)
        {
            std::string output;
            output.reserve(value.size());
            for (std::size_t i = 0u; i < value.size(); ++i)
            {
                char decoded = value[i];
                if (decoded == '%' && i + 2u < value.size())
                {
                    const int high = hexDigit(value[i + 1u]);
                    const int low = hexDigit(value[i + 2u]);
                    if (high >= 0 && low >= 0)
                    {
                        decoded = static_cast<char>((high << 4) | low);
                        i += 2u;
                    }
                }
                output.push_back(static_cast<char>(std::tolower(
                    static_cast<unsigned char>(decoded))));
            }
            return output;
        }

        bool isCredentialQueryKey(const std::string& rawKey)
        {
            static const char* const sensitive[] = {
                "api_key", "apikey", "key", "access_token", "token",
                "bearer", "secret", "signature", "sig", "auth",
                "authorization"
            };
            const std::string key = normalizedQueryKey(rawKey);
            for (const char* value : sensitive)
                if (key == value) return true;
            return false;
        }

        std::string sanitizeSourceUri(const std::string& input)
        {
            if (input.empty()) return input;
            std::string uri = input;
            const std::size_t scheme = uri.find("://");
            if (scheme != std::string::npos)
            {
                const std::size_t authorityBegin = scheme + 3u;
                const std::size_t authorityEnd = uri.find_first_of(
                    "/?#", authorityBegin);
                const std::size_t at = uri.rfind(
                    '@', authorityEnd == std::string::npos
                        ? uri.size() : authorityEnd);
                if (at != std::string::npos && at >= authorityBegin)
                    uri.erase(authorityBegin, at + 1u - authorityBegin);
            }

            const std::size_t fragmentPosition = uri.find('#');
            const std::string fragment = fragmentPosition == std::string::npos
                ? std::string() : uri.substr(fragmentPosition);
            const std::string beforeFragment = fragmentPosition == std::string::npos
                ? uri : uri.substr(0u, fragmentPosition);
            const std::size_t queryPosition = beforeFragment.find('?');
            if (queryPosition == std::string::npos)
                return beforeFragment + fragment;

            const std::string base = beforeFragment.substr(0u, queryPosition);
            const std::string query = beforeFragment.substr(queryPosition + 1u);
            std::vector<std::string> retained;
            std::size_t begin = 0u;
            while (begin <= query.size())
            {
                const std::size_t end = query.find('&', begin);
                const std::string item = query.substr(
                    begin, end == std::string::npos
                        ? std::string::npos : end - begin);
                const std::size_t equals = item.find('=');
                const std::string key = item.substr(0u, equals);
                if (!item.empty() && !isCredentialQueryKey(key))
                    retained.push_back(item);
                if (end == std::string::npos) break;
                begin = end + 1u;
            }

            std::string output = base;
            for (std::size_t i = 0u; i < retained.size(); ++i)
                output += (i == 0u ? "?" : "&") + retained[i];
            return output + fragment;
        }

        const picojson::value* member(const Object& object, const char* key)
        {
            const Object::const_iterator found = object.find(key);
            return found == object.end() ? nullptr : &found->second;
        }

        std::string stringMember(const Object& object, const char* key,
                                 const std::string& fallback = std::string())
        {
            const picojson::value* value = member(object, key);
            return value && value->is<std::string>()
                ? value->get<std::string>() : fallback;
        }

        bool boolMember(const Object& object, const char* key, bool fallback)
        {
            const picojson::value* value = member(object, key);
            return value && value->is<bool>() ? value->get<bool>() : fallback;
        }

        double numberMember(const Object& object, const char* key,
                            double fallback)
        {
            const picojson::value* value = member(object, key);
            return value && value->is<double>() ? value->get<double>() : fallback;
        }

        int integerMember(const Object& object, const char* key, int fallback)
        {
            const double value = numberMember(
                object, key, static_cast<double>(fallback));
            if (!std::isfinite(value) || std::floor(value) != value ||
                value < static_cast<double>(std::numeric_limits<int>::min()) ||
                value > static_cast<double>(std::numeric_limits<int>::max()))
                return fallback;
            return static_cast<int>(value);
        }

        const Object* objectMember(const Object& object, const char* key)
        {
            const picojson::value* value = member(object, key);
            return value && value->is<Object>() ? &value->get<Object>() : nullptr;
        }

        const Array* arrayMember(const Object& object, const char* key)
        {
            const picojson::value* value = member(object, key);
            return value && value->is<Array>() ? &value->get<Array>() : nullptr;
        }

        const char* temporalModeName(TemporalMode mode)
        {
            switch (mode)
            {
            case TemporalMode::Discrete: return "discrete";
            case TemporalMode::Range: return "range";
            case TemporalMode::Live: return "live";
            case TemporalMode::None: default: return "none";
            }
        }

        TemporalMode parseTemporalMode(const std::string& value)
        {
            if (value == "discrete") return TemporalMode::Discrete;
            if (value == "range") return TemporalMode::Range;
            if (value == "live") return TemporalMode::Live;
            return TemporalMode::None;
        }

        Object boundsObject(const Wgs84Bounds& bounds)
        {
            Object output;
            output["valid"] = picojson::value(bounds.valid);
            output["westDeg"] = picojson::value(bounds.westDeg);
            output["southDeg"] = picojson::value(bounds.southDeg);
            output["eastDeg"] = picojson::value(bounds.eastDeg);
            output["northDeg"] = picojson::value(bounds.northDeg);
            return output;
        }

        Object sourceObject(const LayerSourceDescriptor& source)
        {
            Object output;
            output["kind"] = picojson::value(source.kind);
            output["uri"] = picojson::value(source.uri);
            output["providerId"] = picojson::value(source.providerId);
            output["providerVersion"] = picojson::value(source.providerVersion);
            output["crs"] = picojson::value(source.crs);
            output["bounds"] = picojson::value(boundsObject(source.bounds));
            output["attribution"] = picojson::value(source.attribution);
            output["license"] = picojson::value(source.license);
            return output;
        }

        Object renderObject(const LayerRenderDescriptor& render)
        {
            Object output;
            output["kind"] = picojson::value(render.kind);
            output["visible"] = picojson::value(render.visible);
            output["opacity"] = picojson::value(render.opacity);
            output["order"] = picojson::value(static_cast<double>(render.order));
            output["styleId"] = picojson::value(render.styleId);
            output["legendId"] = picojson::value(render.legendId);
            return output;
        }

        Object temporalObject(const TemporalBinding& temporal)
        {
            Object output;
            output["mode"] = picojson::value(temporalModeName(temporal.mode));
            output["dimension"] = picojson::value(temporal.dimension);
            output["granularity"] = picojson::value(temporal.granularity);
            output["currentValue"] = picojson::value(temporal.currentValue);
            Array values;
            for (const std::string& value : temporal.availableValues)
                values.push_back(picojson::value(value));
            output["availableValues"] = picojson::value(values);
            return output;
        }

        Object capabilitiesObject(const LayerCapabilities& capabilities)
        {
            Object output;
            output["temporal"] = picojson::value(capabilities.temporal);
            output["identify"] = picojson::value(capabilities.identify);
            output["selectable"] = picojson::value(capabilities.selectable);
            output["analysis"] = picojson::value(capabilities.analysis);
            output["export"] = picojson::value(capabilities.exportData);
            return output;
        }

        Object layerObject(const EarthLayerDescriptor& layer)
        {
            Object output;
            output["descriptorVersion"] = picojson::value(
                static_cast<double>(layer.descriptorVersion));
            output["id"] = picojson::value(layer.id);
            output["displayName"] = picojson::value(layer.displayName);
            output["group"] = picojson::value(layer.group);
            output["source"] = picojson::value(sourceObject(layer.source));
            output["render"] = picojson::value(renderObject(layer.render));
            output["temporal"] = picojson::value(temporalObject(layer.temporal));
            output["capabilities"] = picojson::value(
                capabilitiesObject(layer.capabilities));
            output["artifactId"] = picojson::value(layer.artifactId);
            return output;
        }

        EarthLayerDescriptor parseLayer(const Object& object)
        {
            EarthLayerDescriptor layer;
            layer.descriptorVersion = integerMember(
                object, "descriptorVersion", kCurrentLayerDescriptorVersion);
            layer.id = stringMember(object, "id");
            layer.displayName = stringMember(object, "displayName");
            layer.group = stringMember(object, "group");
            layer.artifactId = stringMember(object, "artifactId");

            if (const Object* source = objectMember(object, "source"))
            {
                layer.source.kind = stringMember(*source, "kind");
                layer.source.uri = stringMember(*source, "uri");
                layer.source.providerId = stringMember(*source, "providerId");
                layer.source.providerVersion = stringMember(
                    *source, "providerVersion");
                layer.source.crs = stringMember(*source, "crs", "EPSG:4326");
                layer.source.attribution = stringMember(*source, "attribution");
                layer.source.license = stringMember(*source, "license");
                if (const Object* bounds = objectMember(*source, "bounds"))
                {
                    layer.source.bounds.valid = boolMember(*bounds, "valid", false);
                    layer.source.bounds.westDeg = numberMember(*bounds, "westDeg", 0.0);
                    layer.source.bounds.southDeg = numberMember(*bounds, "southDeg", 0.0);
                    layer.source.bounds.eastDeg = numberMember(*bounds, "eastDeg", 0.0);
                    layer.source.bounds.northDeg = numberMember(*bounds, "northDeg", 0.0);
                }
            }

            if (const Object* render = objectMember(object, "render"))
            {
                layer.render.kind = stringMember(*render, "kind");
                layer.render.visible = boolMember(*render, "visible", false);
                layer.render.opacity = numberMember(*render, "opacity", 1.0);
                layer.render.order = integerMember(*render, "order", 0);
                layer.render.styleId = stringMember(*render, "styleId");
                layer.render.legendId = stringMember(*render, "legendId");
            }

            if (const Object* temporal = objectMember(object, "temporal"))
            {
                layer.temporal.mode = parseTemporalMode(
                    stringMember(*temporal, "mode"));
                layer.temporal.dimension = stringMember(*temporal, "dimension");
                layer.temporal.granularity = stringMember(
                    *temporal, "granularity");
                layer.temporal.currentValue = stringMember(
                    *temporal, "currentValue");
                if (const Array* values = arrayMember(*temporal, "availableValues"))
                {
                    for (const picojson::value& value : *values)
                        if (value.is<std::string>())
                            layer.temporal.availableValues.push_back(
                                value.get<std::string>());
                }
            }

            if (const Object* capabilities = objectMember(object, "capabilities"))
            {
                layer.capabilities.temporal = boolMember(
                    *capabilities, "temporal", false);
                layer.capabilities.identify = boolMember(
                    *capabilities, "identify", false);
                layer.capabilities.selectable = boolMember(
                    *capabilities, "selectable", false);
                layer.capabilities.analysis = boolMember(
                    *capabilities, "analysis", false);
                layer.capabilities.exportData = boolMember(
                    *capabilities, "export", false);
            }
            return layer;
        }

        EarthProject parseV1(const Object& root)
        {
            EarthProject project;
            project.projectId = stringMember(
                root, "projectId", "untitled-project");
            project.name = stringMember(
                root, "name", "Untitled Earth Project");
            if (const Object* camera = objectMember(root, "camera"))
            {
                project.camera.latitudeDeg = numberMember(
                    *camera, "latitudeDeg", project.camera.latitudeDeg);
                project.camera.longitudeDeg = numberMember(
                    *camera, "longitudeDeg", project.camera.longitudeDeg);
                project.camera.altitudeKm = numberMember(
                    *camera, "altitudeKm", project.camera.altitudeKm);
                project.camera.headingDeg = numberMember(
                    *camera, "headingDeg", project.camera.headingDeg);
                project.camera.pitchDeg = numberMember(
                    *camera, "pitchDeg", project.camera.pitchDeg);
                project.camera.rollDeg = numberMember(
                    *camera, "rollDeg", project.camera.rollDeg);
            }
            if (const Array* layers = arrayMember(root, "layers"))
            {
                for (const picojson::value& value : *layers)
                    if (value.is<Object>())
                        project.layers.push_back(parseLayer(value.get<Object>()));
            }
            if (const Object* workspace = objectMember(root, "workspace"))
            {
                project.workspace.activeModule = stringMember(
                    *workspace, "activeModule", "explore");
                project.workspace.drawerOpen = boolMember(
                    *workspace, "drawerOpen", true);
                if (const Array* ids = arrayMember(
                        *workspace, "activeArtifactIds"))
                {
                    for (const picojson::value& value : *ids)
                        if (value.is<std::string>())
                            project.workspace.activeArtifactIds.push_back(
                                value.get<std::string>());
                }
            }
            return project;
        }

        EarthProject migrateV0(const Object& root)
        {
            EarthProject project;
            project.name = stringMember(root, "name", "Untitled Earth Project");
            project.projectId = stringMember(root, "projectId", "untitled-project");
            if (const Object* camera = objectMember(root, "camera"))
            {
                project.camera.latitudeDeg = numberMember(
                    *camera, "lat", project.camera.latitudeDeg);
                project.camera.longitudeDeg = numberMember(
                    *camera, "lon", project.camera.longitudeDeg);
                project.camera.altitudeKm = numberMember(
                    *camera, "altKm", project.camera.altitudeKm);
            }
            if (const Array* layers = arrayMember(root, "layers"))
            {
                for (const picojson::value& value : *layers)
                {
                    if (!value.is<Object>()) continue;
                    const Object& oldLayer = value.get<Object>();
                    EarthLayerDescriptor layer;
                    layer.id = stringMember(oldLayer, "id");
                    layer.displayName = stringMember(oldLayer, "name");
                    layer.render.visible = boolMember(oldLayer, "enabled", false);
                    layer.render.opacity = numberMember(oldLayer, "opacity", 1.0);
                    project.layers.push_back(layer);
                }
            }
            return project;
        }
    }

    EarthProject normalizeProject(
        const EarthProject& input,
        std::vector<ProjectDiagnostic>* diagnostics)
    {
        EarthProject project = input;
        project.schemaVersion = kCurrentSchemaVersion;
        if (project.projectId.empty())
        {
            project.projectId = "untitled-project";
            diagnose(diagnostics, "missing_project_id",
                     "Project id was replaced with a stable default");
        }
        if (project.name.empty())
        {
            project.name = "Untitled Earth Project";
            diagnose(diagnostics, "missing_project_name",
                     "Project name was replaced with a readable default");
        }

        project.camera.latitudeDeg = clampNumber(
            finiteOr(project.camera.latitudeDeg, 0.0, diagnostics,
                     "camera.latitudeDeg"),
            -90.0, 90.0, diagnostics, "camera.latitudeDeg");
        project.camera.longitudeDeg = normalizeLongitude(finiteOr(
            project.camera.longitudeDeg, 0.0, diagnostics,
            "camera.longitudeDeg"));
        project.camera.altitudeKm = clampNumber(
            finiteOr(project.camera.altitudeKm, 38119.0, diagnostics,
                     "camera.altitudeKm"),
            0.0, 1000000.0, diagnostics, "camera.altitudeKm");
        project.camera.headingDeg = normalizeHeading(finiteOr(
            project.camera.headingDeg, 0.0, diagnostics,
            "camera.headingDeg"));
        project.camera.pitchDeg = clampNumber(
            finiteOr(project.camera.pitchDeg, -90.0, diagnostics,
                     "camera.pitchDeg"),
            -90.0, 90.0, diagnostics, "camera.pitchDeg");
        project.camera.rollDeg = normalizeSignedAngle(finiteOr(
            project.camera.rollDeg, 0.0, diagnostics,
            "camera.rollDeg"));

        std::vector<EarthLayerDescriptor> layers;
        std::set<std::string> layerIds;
        for (EarthLayerDescriptor layer : project.layers)
        {
            if (layer.id.empty())
            {
                diagnose(diagnostics, "empty_layer_id",
                         "A layer without an id was omitted");
                continue;
            }
            if (!layerIds.insert(layer.id).second)
            {
                diagnose(diagnostics, "duplicate_layer_id",
                         "Duplicate layer '" + layer.id + "' was omitted");
                continue;
            }
            layer.descriptorVersion = kCurrentLayerDescriptorVersion;
            layer.render.opacity = clampNumber(
                finiteOr(layer.render.opacity, 1.0, diagnostics,
                         "layer.render.opacity"),
                0.0, 1.0, diagnostics, "layer.render.opacity");
            const std::string safeUri = sanitizeSourceUri(layer.source.uri);
            if (safeUri != layer.source.uri)
            {
                layer.source.uri = safeUri;
                diagnose(diagnostics, "credential_material_removed",
                         "Credential material was removed from a layer URI");
            }
            if (layer.source.crs.empty()) layer.source.crs = "EPSG:4326";
            if (layer.source.bounds.valid)
            {
                layer.source.bounds.westDeg = normalizeLongitude(finiteOr(
                    layer.source.bounds.westDeg, 0.0, diagnostics,
                    "layer.source.bounds.westDeg"));
                layer.source.bounds.eastDeg = normalizeLongitude(finiteOr(
                    layer.source.bounds.eastDeg, 0.0, diagnostics,
                    "layer.source.bounds.eastDeg"));
                layer.source.bounds.southDeg = clampNumber(finiteOr(
                    layer.source.bounds.southDeg, 0.0, diagnostics,
                    "layer.source.bounds.southDeg"),
                    -90.0, 90.0, diagnostics,
                    "layer.source.bounds.southDeg");
                layer.source.bounds.northDeg = clampNumber(finiteOr(
                    layer.source.bounds.northDeg, 0.0, diagnostics,
                    "layer.source.bounds.northDeg"),
                    -90.0, 90.0, diagnostics,
                    "layer.source.bounds.northDeg");
                if (layer.source.bounds.southDeg >
                    layer.source.bounds.northDeg)
                {
                    std::swap(layer.source.bounds.southDeg,
                              layer.source.bounds.northDeg);
                    diagnose(diagnostics, "bounds_latitude_swapped",
                             "Layer bounds latitude order was corrected");
                }
            }
            std::vector<std::string> times;
            std::set<std::string> seenTimes;
            for (const std::string& value : layer.temporal.availableValues)
                if (!value.empty() && seenTimes.insert(value).second)
                    times.push_back(value);
            layer.temporal.availableValues.swap(times);
            layers.push_back(layer);
        }
        project.layers.swap(layers);

        std::vector<std::string> artifactIds;
        std::set<std::string> seenArtifacts;
        for (const std::string& id : project.workspace.activeArtifactIds)
            if (!id.empty() && seenArtifacts.insert(id).second)
                artifactIds.push_back(id);
        project.workspace.activeArtifactIds.swap(artifactIds);
        if (project.workspace.activeModule.empty())
            project.workspace.activeModule = "explore";
        return project;
    }

    std::string saveProjectJson(const EarthProject& input)
    {
        const EarthProject project = normalizeProject(input);
        Object root;
        root["schema"] = picojson::value(kProjectSchema);
        root["version"] = picojson::value(
            static_cast<double>(kCurrentSchemaVersion));
        root["projectId"] = picojson::value(project.projectId);
        root["name"] = picojson::value(project.name);

        Object camera;
        camera["latitudeDeg"] = picojson::value(project.camera.latitudeDeg);
        camera["longitudeDeg"] = picojson::value(project.camera.longitudeDeg);
        camera["altitudeKm"] = picojson::value(project.camera.altitudeKm);
        camera["headingDeg"] = picojson::value(project.camera.headingDeg);
        camera["pitchDeg"] = picojson::value(project.camera.pitchDeg);
        camera["rollDeg"] = picojson::value(project.camera.rollDeg);
        root["camera"] = picojson::value(camera);

        Array layers;
        for (const EarthLayerDescriptor& layer : project.layers)
            layers.push_back(picojson::value(layerObject(layer)));
        root["layers"] = picojson::value(layers);

        Object workspace;
        workspace["activeModule"] = picojson::value(
            project.workspace.activeModule);
        workspace["drawerOpen"] = picojson::value(
            project.workspace.drawerOpen);
        Array artifacts;
        for (const std::string& id : project.workspace.activeArtifactIds)
            artifacts.push_back(picojson::value(id));
        workspace["activeArtifactIds"] = picojson::value(artifacts);
        root["workspace"] = picojson::value(workspace);
        return picojson::value(root).serialize();
    }

    ProjectLoadResult loadProjectJson(const std::string& json)
    {
        ProjectLoadResult result;
        if (json.size() > kMaxProjectJsonBytes)
        {
            result.errorCode = "project_too_large";
            result.errorMessage = "Project exceeds the 16 MiB JSON limit";
            return result;
        }
        picojson::value value;
        const std::string parseError = picojson::parse(value, json);
        if (!parseError.empty() || !value.is<Object>())
        {
            result.errorCode = "invalid_project_json";
            result.errorMessage = parseError.empty()
                ? "Project root must be a JSON object" : parseError;
            return result;
        }

        const Object& root = value.get<Object>();
        if (stringMember(root, "schema") != kProjectSchema)
        {
            result.errorCode = "unsupported_project_schema";
            result.errorMessage = "Project schema is not osgsol-earth-project";
            return result;
        }
        const int version = integerMember(root, "version", -1);
        if (version < 0 || version > kCurrentSchemaVersion)
        {
            result.errorCode = "unsupported_project_version";
            result.errorMessage = "Project version is not supported";
            return result;
        }

        if (version == 0)
        {
            result.project = migrateV0(root);
            result.migratedFromVersion = 0;
            diagnose(&result.diagnostics, "project_migrated",
                     "Legacy project v0 was migrated to v1");
        }
        else result.project = parseV1(root);

        result.project = normalizeProject(result.project, &result.diagnostics);
        result.ok = true;
        return result;
    }
}
