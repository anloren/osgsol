uniform sampler2D SceneSampler, MaskSampler, ExtraLayerSampler, Overlay2Sampler;
uniform sampler2D TransmittanceSampler;
uniform sampler2D SkyIrradianceSampler;
uniform sampler3D InscatterSampler;
uniform sampler2D GlareSampler;
uniform sampler2D ScienceOverlaySampler;
uniform vec4 UvOffset1, UvOffset2, UvOffset3, UvOffset4;
uniform vec4 TerrainMapBounds, ScienceOverlayBounds;
uniform vec2 TerrainMapOriginHigh, TerrainMapOriginLow, TerrainMapSpan;
uniform vec2 ScienceOverlayOriginHigh, ScienceOverlayOriginLow;
uniform vec2 ScienceOverlaySpan;
uniform vec3 WorldCameraPos, WorldSunDir, EarthOrigin;
uniform float HdrExposure, GlobalOpaque, LabelOpacity, Overlay2Opacity;
uniform float ScienceOverlayOpacity;
uniform float ClarityAltLo, ClarityAltHi;
uniform bool TerrainUsesWebMercator;
uniform bool ScienceOverlayVisible, ScienceOverlayOutlineVisible;

VERSE_FS_IN vec3 vertexInWorld, normalInWorld;
VERSE_FS_IN vec4 texCoord;
VERSE_FS_IN float isSkirt;

#ifdef VERSE_GLES3
layout(location = 0) VERSE_FS_OUT vec4 fragColor;
layout(location = 1) VERSE_FS_OUT vec4 fragOrigin;
#endif

#define SUN_INTENSITY 100.0
#define PLANET_RADIUS 6360000.0
#include "scattering.module.glsl"
#include "scattering_globe_ground.module.glsl"

vec3 hdr(vec3 L)
{
    L = L * HdrExposure;
    L.r = L.r < 1.413 ? pow(L.r * 0.38317, 1.0 / 2.2) : 1.0 - exp(-L.r);
    L.g = L.g < 1.413 ? pow(L.g * 0.38317, 1.0 / 2.2) : 1.0 - exp(-L.g);
    L.b = L.b < 1.413 ? pow(L.b * 0.38317, 1.0 / 2.2) : 1.0 - exp(-L.b);
    float luma = dot(L, vec3(0.299, 0.587, 0.114));
    L = mix(vec3(luma), L, 1.08);
    L = clamp((L - 0.5) * 1.06 + 0.5, 0.0, 1.0);
    return L;
}

bool terrainScienceUv(vec2 tileUv, out vec2 scienceUv)
{
    float longitudeDelta =
        (TerrainMapOriginHigh.x - ScienceOverlayOriginHigh.x) +
        (TerrainMapOriginLow.x - ScienceOverlayOriginLow.x) +
        TerrainMapSpan.x * tileUv.x;
    float mapY = TerrainMapOriginHigh.y + TerrainMapOriginLow.y +
        TerrainMapSpan.y * tileUv.y;
    float latitude = TerrainUsesWebMercator
        ? degrees(atan(sinh(radians(2.0 * mapY)))) : mapY;
    float latitudeDelta =
        (latitude - ScienceOverlayOriginHigh.y) -
        ScienceOverlayOriginLow.y;
    scienceUv = vec2(longitudeDelta / ScienceOverlaySpan.x,
                     latitudeDelta / ScienceOverlaySpan.y);
    bool insideScienceBounds =
        scienceUv.x >= 0.0 && scienceUv.x <= 1.0 &&
        scienceUv.y >= 0.0 && scienceUv.y <= 1.0;
    return insideScienceBounds;
}

vec4 applyScienceOverlay(vec4 groundColor, vec2 tileUv)
{
    if (!ScienceOverlayVisible) return groundColor;
    vec2 scienceUv;
    if (!terrainScienceUv(tileUv, scienceUv)) return groundColor;
    vec4 scienceColor = VERSE_TEX2D(ScienceOverlaySampler, scienceUv);
    float scienceAlpha = scienceColor.a *
        clamp(ScienceOverlayOpacity, 0.0, 1.0);
    if (ScienceOverlayOutlineVisible && scienceColor.a > 0.0)
    {
        float edge = min(min(scienceUv.x, scienceUv.y),
                         min(1.0 - scienceUv.x, 1.0 - scienceUv.y));
        float outline = 1.0 - smoothstep(0.0, 0.004, edge);
        scienceColor.rgb = mix(
            scienceColor.rgb, vec3(1.0, 0.82, 0.18), outline * 0.8);
    }
    groundColor.rgb = mix(groundColor.rgb, scienceColor.rgb, scienceAlpha);
    return groundColor;
}

void main()
{
    vec4 worldPos = vec4(vertexInWorld, 1.0);
    vec4 groundColor = VERSE_TEX2D(
        SceneSampler, texCoord.st * UvOffset1.zw + UvOffset1.xy);
    groundColor = applyScienceOverlay(groundColor, texCoord.st);
    groundColor = composeGroundLayers(groundColor, texCoord.st);
    if (isSkirt < -0.1 && GlobalOpaque < 0.9) discard;

    vec2 uv = texCoord.xy * UvOffset2.zw + UvOffset2.xy;
    vec4 maskColor = VERSE_TEX2D(MaskSampler, uv.st);
    float off = 0.002, maskValue = maskColor.z;
    maskColor += VERSE_TEX2D(MaskSampler, uv.st + vec2(-off, 0.0));
    maskColor += VERSE_TEX2D(MaskSampler, uv.st + vec2(off, 0.0));
    maskColor += VERSE_TEX2D(MaskSampler, uv.st + vec2(0.0, -off));
    maskColor += VERSE_TEX2D(MaskSampler, uv.st + vec2(0.0, off));
    maskColor += VERSE_TEX2D(MaskSampler, uv.st + vec2(-off, -off));
    maskColor += VERSE_TEX2D(MaskSampler, uv.st + vec2(off, -off));
    maskColor += VERSE_TEX2D(MaskSampler, uv.st + vec2(off, off));
    maskColor += VERSE_TEX2D(MaskSampler, uv.st + vec2(-off, off));
    maskColor *= 1.0 / 9.0;

    vec3 WSD = WorldSunDir, WCP = WorldCameraPos;
    vec3 P = vertexInWorld, N = normalize(P);
    P = N * (length(P) * 0.99);

    vec3 originalGroundColor = groundColor.rgb;
    float cTheta = max(dot(N, WSD), 0.0); vec3 sunL, skyE;
    sunRadianceAndSkyIrradiance(P, N, WSD, sunL, skyE);
    groundColor.rgb *= max(
        (sunL * cTheta + skyE) / 3.14159265, vec3(0.1));
    groundColor.a *= clamp(GlobalOpaque, 0.0, 1.0);

    vec3 extinction = vec3(1.0);
    vec3 inscatter = inScattering(WCP, P, WSD, extinction, 0.0) * 0.5;
    vec3 compositeColor = groundColor.rgb * extinction + inscatter;
    vec3 spaceColor = mix(hdr(compositeColor), originalGroundColor, cTheta);
    float dayNight = mix(
        0.06, 1.0, smoothstep(-0.05, 0.25, dot(N, WSD)));
    vec3 clearColor = originalGroundColor * dayNight;
    float camAlt = length(WCP) - PLANET_RADIUS;
    float groundClarity = smoothstep(ClarityAltHi, ClarityAltLo, camAlt);
    vec4 finalColor = vec4(
        mix(spaceColor, clearColor, groundClarity), groundColor.a);

#ifdef VERSE_GLES3
    fragColor = finalColor;
    fragOrigin = vec4(1.0 - maskValue);
#else
    gl_FragData[0] = finalColor;
    gl_FragData[1] = vec4(1.0 - maskValue);
#endif
}
