#ifndef RTXDI_PT_APPLICATION_BRIDGE_HLSLI
#define RTXDI_PT_APPLICATION_BRIDGE_HLSLI

// ReSTIR PT Application Bridge
// Implements the RAB_* interface functions required by the RTXDI PT library.
// Designed to be compatible with ALL existing material models in this project.

#include "ReSTIRPT/Registers.hlsli"

// Material type required by StandardBSDF
#include "interop/Material.hlsli"

// Include advanced settings for DIFFUSE_MODE defaults
#include "raytracing/include/AdvancedSettings.hlsli"

// Full BSDF system
#include "raytracing/include/Materials/BSDF.hlsli"
#include "raytracing/include/Rays.hlsli"
#include "include/Lighting.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Common/Color.hlsli"
#include "raytracing/include/RayOffset.hlsli"
#include "Rtxdi/Utils/SampledLightData.hlsli"

// RTXDI types
#define RAB_DISTANT_LIGHT_DISTANCE 50000.0f
#include <Rtxdi/PT/Reservoir.hlsli>
#include <Rtxdi/Utils/RandomSamplerState.hlsli>

// ---------------------------------------------------------------------------
// RAB_Surface — reconstructed from PackedSurfaceData
// ---------------------------------------------------------------------------
struct RAB_Surface
{
    Surface surface;
    BRDFContext brdfContext;
    uint materialFeature;
    bool hasSpecularTransmission;
    bool isEnter;
    float viewDepth;
    uint materialIndex;
    uint instanceIndex;
    uint psrData;
    float3 pathThroughput;

    float4 Eval(float3 wo)
    {
        Material material = GetMaterial(materialIndex);
        StandardBSDF bsdf = StandardBSDF::make(surface, isEnter);
        return bsdf.Eval(brdfContext, material, surface, wo);
    }

    float EvalPdf(float3 wo)
    {
        StandardBSDF bsdf = StandardBSDF::make(surface, isEnter);
        return bsdf.EvalPdf(brdfContext, surface, wo);
    }
};

static const float RAB_BACKGROUND_DEPTH = 1e4f;

float3 RAB_UnpackPathThroughput(uint packed)
{
    return packed != 0u ? PSD_UnpackColor(packed) : float3(1.0f, 1.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Unpack PackedSurfaceData -> full Surface
// ---------------------------------------------------------------------------
Surface PSD_UnpackToSurfacePT(PackedSurfaceData d)
{
    Surface s = (Surface)0;
    s.Primary      = true;
    s.Position     = d.posW;
    s.PrevPosition = d.posW;
    s.CameraRelativePosition = d.posW - Camera.Position.xyz;
    s.PrevCameraRelativePosition = s.CameraRelativePosition + (Camera.Position.xyz - Camera.PositionPrev.xyz);
    s.Normal       = PSD_UnpackOct(d.packedNormal);
    s.Tangent      = PSD_UnpackOct(d.packedTangent);
    s.Bitangent    = PSD_UnpackOct(d.packedBitangent);
    s.GeomNormal   = s.Normal;
    s.GeomTangent  = s.Tangent;
    s.FaceNormal   = PSD_UnpackOct(d.packedFaceNormal);
    s.DiffuseAlbedo = PSD_UnpackColor(d.diffuseAlbedo);
    s.F0           = PSD_UnpackColor(d.specularF0);
    s.Roughness    = f16tof32(d.roughMetallic & 0xFFFF);
    s.Metallic     = f16tof32(d.roughMetallic >> 16);
    s.Albedo       = s.Metallic > 0.5 ? s.F0 : s.DiffuseAlbedo;
    s.Alpha        = 1.0;

    float maxF0 = max(max(s.F0.r, s.F0.g), s.F0.b);
    s.IOR = (1.0 + sqrt(maxF0)) / max(1.0 - sqrt(maxF0), 1e-5);

    s.Emissive           = 0;
    s.AO                 = 1.0;
    s.TransmissionColor  = 0;
    s.VolumeAbsorption   = 0;
    s.SubsurfaceData     = (Subsurface)0;
    s.DiffTrans          = 0;
    s.SpecTrans          = 0;
    s.IsThinSurface      = PSD_SurfaceIsThinSurface(d);
    s.CoatColor          = 1;
    s.CoatStrength       = 0;
    s.CoatRoughness      = 0;
    s.CoatF0             = 0.04;
    s.CoatNormal         = s.Normal;
    s.CoatTangent        = s.Tangent;
    s.CoatBitangent      = s.Bitangent;
    s.FuzzColor          = 0;
    s.FuzzWeight         = 0;
    s.MipLevel           = 0;
    s.PositionError      = CalculatePositionError(s.Position);
#if USE_SIA_INTERPOLATION
    s.SIAOffset          = 0.0f;
#endif

    return s;
}

// ---------------------------------------------------------------------------
// Load packed surface from ping-pong buffer
// ---------------------------------------------------------------------------
RAB_Surface LoadPTSurfaceFromBuffer(uint2 pixelPosition, bool previousFrame)
{
    RAB_Surface rab;

    uint plane = previousFrame ? ((Camera.FrameIndex + 1) % 2) : (Camera.FrameIndex % 2);
    uint2 sz = Camera.RenderSize;
    uint linearIdx = plane * (sz.x * sz.y) + pixelPosition.y * sz.x + pixelPosition.x;

    PackedSurfaceData packed = SurfaceDataBuffer[linearIdx];

    if (PSD_IsEmpty(packed))
    {
        rab.surface = (Surface)0;
        rab.surface.Normal = float3(0, 0, 1);
        rab.surface.FaceNormal = float3(0, 0, 1);
        rab.brdfContext = BRDFContext::make(rab.surface, float3(0, 0, 1));
        rab.materialFeature = Feature::kDefault;
        rab.hasSpecularTransmission = false;
        rab.isEnter = true;
        rab.viewDepth = RAB_BACKGROUND_DEPTH;
        rab.materialIndex = 0;
        rab.instanceIndex = 0;
        rab.psrData = 0;
        rab.pathThroughput = 0.0f;
        return rab;
    }

    rab.surface = PSD_UnpackToSurfacePT(packed);
    float3 viewDir = PSD_UnpackOct(packed.packedViewDir);
    rab.brdfContext = BRDFContext::make(rab.surface, viewDir);
    rab.materialFeature = PSD_GetMaterialFeature(packed);
    rab.hasSpecularTransmission = PSD_SurfaceHasSpecularTransmission(packed);
    rab.isEnter = PSD_SurfaceIsEnterSurface(packed);
    rab.viewDepth = packed.viewDepth;
    rab.materialIndex = packed.materialIndex;
    rab.instanceIndex = packed.instanceIndex;
    rab.psrData = packed.psrData;
    rab.pathThroughput = RAB_UnpackPathThroughput(packed.psrData);

    return rab;
}

// ---------------------------------------------------------------------------
// RAB_Surface construction helpers
// ---------------------------------------------------------------------------
RAB_Surface RAB_EmptySurface()
{
    RAB_Surface rab;
    rab.surface = (Surface)0;
    rab.surface.Normal = float3(0, 0, 1);
    rab.surface.FaceNormal = float3(0, 0, 1);
    rab.brdfContext = BRDFContext::make(rab.surface, float3(0, 0, 1));
    rab.materialFeature = Feature::kDefault;
    rab.hasSpecularTransmission = false;
    rab.isEnter = true;
    rab.viewDepth = RAB_BACKGROUND_DEPTH;
    rab.materialIndex = 0;
    rab.instanceIndex = 0;
    rab.psrData = 0;
    rab.pathThroughput = 0.0f;
    return rab;
}

RAB_Surface RAB_MakeSurfaceFromPayload(float3 position, Payload payload, float3 rayDir, RayCone rayCone, bool primary)
{
    RAB_Surface rab;
    Instance instance;
    Material material;
    rab.surface = SurfaceMaker::make(position, payload, rayDir, rayCone, instance, material, primary);
    rab.brdfContext = BRDFContext::make(rab.surface, -rayDir);
    bool isEnter = dot(rab.surface.FaceNormal, rab.brdfContext.ViewDirection) >= 0.0f;
    rab.isEnter = isEnter;
    if (!isEnter)
    {
        rab.surface.FlipNormal();
        rab.brdfContext.NdotV = saturate(dot(rab.surface.Normal, rab.brdfContext.ViewDirection));
    }
    AdjustShadingNormal(rab.surface, rab.brdfContext, true, false);
    rab.materialFeature = material.Feature;
    rab.hasSpecularTransmission = rab.surface.SpecTrans > 0.0f;
    rab.viewDepth = length(rab.surface.Position - Camera.Position.xyz);
    Mesh mesh = GetMesh(payload, instance);
    rab.materialIndex = mesh.GeometryIdx;
    rab.instanceIndex = payload.GetInstanceIndex();
    rab.psrData = 0;
    rab.pathThroughput = 1.0f;
    return rab;
}

bool RAB_IsSurfaceValid(RAB_Surface rab)
{
    return rab.viewDepth < RAB_BACKGROUND_DEPTH;
}

float RAB_GetSurfaceLinearDepth(RAB_Surface rab)
{
    return rab.viewDepth;
}

float3 RAB_GetSurfaceNormal(RAB_Surface rab)
{
    return rab.surface.Normal;
}

void RAB_SetSurfaceNormal(inout RAB_Surface rab, float3 normal)
{
    rab.surface.Normal = normal;
}

void RAB_SetSurfaceWorldPos(inout RAB_Surface rab, float3 worldPos)
{
    rab.surface.Position = worldPos;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface rab)
{
    return rab.surface.Position;
}

float RAB_GetSurfaceRoughness(RAB_Surface rab)
{
    return rab.surface.Roughness;
}

float3 RAB_GetSurfaceViewDir(RAB_Surface rab)
{
    return rab.brdfContext.ViewDirection;
}

// ---------------------------------------------------------------------------
// GBuffer surface access
// ---------------------------------------------------------------------------
RAB_Surface RAB_GetGBufferSurface(uint2 pixelPosition, bool previousFrame)
{
    if (any(pixelPosition >= Camera.RenderSize))
        return RAB_EmptySurface();

    return LoadPTSurfaceFromBuffer(pixelPosition, previousFrame);
}

// ---------------------------------------------------------------------------
// Material similarity check
// ---------------------------------------------------------------------------
typedef RAB_Surface RAB_Material;

RAB_Material RAB_GetMaterial(RAB_Surface rab)
{
    return rab;
}

bool RAB_AreMaterialsSimilar(RAB_Material a, RAB_Material b)
{
    bool aIsHair = a.materialFeature == Feature::kHairTint;
    bool bIsHair = b.materialFeature == Feature::kHairTint;
    if (aIsHair != bIsHair)
        return false;

    if (a.materialFeature != b.materialFeature)
        return false;

    if (a.hasSpecularTransmission != b.hasSpecularTransmission)
        return false;

    if (a.hasSpecularTransmission || b.hasSpecularTransmission)
    {
        if (a.surface.IsThinSurface != b.surface.IsThinSurface)
            return false;

        if (!a.surface.IsThinSurface && a.isEnter != b.isEnter)
            return false;
    }

    if (!RTXDI_CompareRelativeDifference(a.surface.Roughness, b.surface.Roughness, 0.5))
        return false;

    float reflA = Color::RGBToLuminance(a.surface.F0);
    float reflB = Color::RGBToLuminance(b.surface.F0);
    if (abs(reflA - reflB) > 0.25)
        return false;

    float albedoA = Color::RGBToLuminance(a.surface.DiffuseAlbedo);
    float albedoB = Color::RGBToLuminance(b.surface.DiffuseAlbedo);
    if (abs(albedoA - albedoB) > 0.25)
        return false;

    return true;
}

bool RAB_IsTwoSidedMaterial(RAB_Surface rab)
{
    Material material = GetMaterial(rab.materialIndex);
    return (material.ShaderFlags & ShaderFlags::kTwoSided) != 0;
}

bool RAB_IsDirectionUsableForOpaqueReflection(RAB_Surface rab, float3 direction)
{
    if (rab.hasSpecularTransmission || rab.surface.SpecTrans > 0.0f || rab.surface.DiffTrans > 0.0f)
        return true;

    if (RAB_IsTwoSidedMaterial(rab))
        return true;

    return dot(rab.surface.FaceNormal, direction) > 1e-4f;
}

// ---------------------------------------------------------------------------
// Target PDF through BSDF evaluation for PT
// ---------------------------------------------------------------------------
float3 RAB_GetPTSampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface rab)
{
    float3 L = normalize(samplePosition - rab.surface.Position);
    if (!RAB_IsDirectionUsableForOpaqueReflection(rab, L))
        return 0.0f;

    Material material = GetMaterial(rab.materialIndex);
    StandardBSDF bsdf = StandardBSDF::make(rab.surface, rab.isEnter);
    return max(EvalLight(L, material, rab.surface, rab.brdfContext, bsdf) * sampleRadiance * rab.pathThroughput, 0.0f);
}

// ---------------------------------------------------------------------------
// BSDF PDF evaluation for reconnection checks
// ---------------------------------------------------------------------------
float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface rab, float3 wo)
{
    return rab.EvalPdf(wo);
}

// ---------------------------------------------------------------------------
// Reflected BSDF radiance
// ---------------------------------------------------------------------------
float3 RAB_GetReflectedBsdfRadianceForSurface(float3 lightPos, float3 lightRadiance, RAB_Surface rab)
{
    float3 L = normalize(lightPos - rab.surface.Position);
    if (!RAB_IsDirectionUsableForOpaqueReflection(rab, L))
        return 0.0f;

    Material material = GetMaterial(rab.materialIndex);
    StandardBSDF bsdf = StandardBSDF::make(rab.surface, rab.isEnter);
    return EvalLight(L, material, rab.surface, rab.brdfContext, bsdf) * lightRadiance * rab.pathThroughput;
}

// ---------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------
bool RAB_GetConservativeVisibility(RAB_Surface rab, float3 samplePosition)
{
    float3 toSample = samplePosition - rab.surface.Position;
    float dist = length(toSample);

    if (dist < 1e-4)
        return true;

    if (!RAB_IsDirectionUsableForOpaqueReflection(rab, toSample / dist))
        return false;

    bool behindSurface = dot(toSample, rab.surface.FaceNormal) < 0.0f;
    float3 origin = OffsetRayAlt(rab.surface.Position, rab.surface.FaceNormal, behindSurface);

    float3 offsetToSample = samplePosition - origin;
    float offsetDist = length(offsetToSample);

    if (offsetDist < 1e-4)
        return true;

    const float offset = 0.001f;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = offsetToSample / offsetDist;
    ray.TMin = 0.0f;
    ray.TMax = max(0.0f, offsetDist - offset);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> rayQuery;
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_CULL_NON_OPAQUE, INSTANCE_MASK, ray);
    rayQuery.Proceed();

    return rayQuery.CommittedStatus() == COMMITTED_NOTHING;
}

bool RAB_GetTemporalConservativeVisibility(RAB_Surface surface, RAB_Surface temporalSurface, float3 samplePosition)
{
    return RAB_GetConservativeVisibility(surface, samplePosition);
}

// ---------------------------------------------------------------------------
// Viewport clamping and random sampler alias
// ---------------------------------------------------------------------------
int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool previousFrame)
{
    return clamp(pixelPosition, int2(0, 0), int2(Camera.RenderSize) - int2(1, 1));
}

typedef RTXDI_RandomSamplerState RAB_RandomSamplerState;

// ---------------------------------------------------------------------------
// Duplication map (stub for now)
// ---------------------------------------------------------------------------
uint RAB_GetDuplicationMapCount(int2 prevPos)
{
    return 1;
}

// ---------------------------------------------------------------------------
// RAB_PathTracerUserData — minimal struct for path type tracking
// ---------------------------------------------------------------------------
struct RAB_PathTracerUserData
{
    uint pathType;
    uint2 pixelPosition;
    float3 psrMotionVector;
    float psrDepth;
    float3 psrNormal;
    float psrRoughness;
    float3 psrRadiance;
    uint psrValid;
};

RAB_PathTracerUserData RAB_EmptyPathTracerUserData(uint2 pixelPosition)
{
    RAB_PathTracerUserData ptud = (RAB_PathTracerUserData)0;
    ptud.pixelPosition = pixelPosition;
    return ptud;
}

void RAB_PathTracerUserDataSetPathType(inout RAB_PathTracerUserData ptud, uint pathType)
{
    ptud.pathType = pathType;
}

// ---------------------------------------------------------------------------
// Denoiser callbacks (no-ops for now; PSR not implemented in initial version)
// ---------------------------------------------------------------------------
void RAB_ReconnectionDenoiserCallback(RTXDI_PTReservoir reservoir, RAB_Surface surface, inout RAB_PathTracerUserData ptud)
{
    ptud.psrNormal = surface.surface.Normal;
    ptud.psrRoughness = surface.surface.Roughness;
    ptud.psrDepth = RAB_GetSurfaceLinearDepth(surface);
    ptud.psrValid = 1;
}

void RAB_LastBounceDenoiserCallback(float3 lightPos, RAB_Surface surface, inout RAB_PathTracerUserData ptud)
{
    ptud.psrNormal = surface.surface.Normal;
    ptud.psrRoughness = surface.surface.Roughness;
    ptud.psrDepth = RAB_GetSurfaceLinearDepth(surface);
    ptud.psrValid = 1;
}

// ---------------------------------------------------------------------------
// Light bridge for ReSTIR PT NEE reconnection
// ---------------------------------------------------------------------------
static const uint RAB_LIGHT_TYPE_DIRECTIONAL = 0;
static const uint RAB_LIGHT_TYPE_POINT = 1;
static const uint RAB_LIGHT_TYPE_SKY = 2;

struct RAB_LightInfo
{
    uint lightType;
    uint lightIndex;
};

struct RAB_LightSample
{
    float3 position;
    float3 radiance;
    float solidAnglePdf;
    float distance;
    uint lightType;
    uint lightIndex;
};

bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample) { return lightSample.solidAnglePdf > 0.0f && lightSample.lightType != RAB_LIGHT_TYPE_SKY; }
bool RAB_IsInfiniteLightSample(RAB_LightSample lightSample) { return lightSample.lightType == RAB_LIGHT_TYPE_DIRECTIONAL || lightSample.lightType == RAB_LIGHT_TYPE_SKY; }

RAB_LightInfo RAB_EmptyLightInfo() { RAB_LightInfo li; li.lightType = 0; li.lightIndex = 0; return li; }
RAB_LightSample RAB_EmptyLightSample() { RAB_LightSample ls; ls.position = 0; ls.radiance = 0; ls.solidAnglePdf = 0; ls.distance = 0; ls.lightType = 0; ls.lightIndex = 0; return ls; }

float3 RAB_LightSamplePosition(RAB_LightSample ls) { return ls.position; }
float3 RAB_LightSampleRadiance(RAB_LightSample ls) { return ls.radiance; }
float RAB_LightSampleSolidAnglePdf(RAB_LightSample ls) { return ls.solidAnglePdf; }

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample, out float3 lightDir, out float lightDistance)
{
    if (lightSample.lightType == RAB_LIGHT_TYPE_SKY)
    {
        lightDir = normalize(lightSample.position - surface.surface.Position);
        lightDistance = RAB_DISTANT_LIGHT_DISTANCE;
        return;
    }

    float3 toLight = lightSample.position - surface.surface.Position;
    lightDistance = length(toLight);
    lightDir = toLight / max(lightDistance, 1e-4f);
}

float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    if (lightSample.solidAnglePdf <= 0.0f)
        return 0.0f;
    return Color::RGBToLuminance(RAB_GetReflectedBsdfRadianceForSurface(lightSample.position, lightSample.radiance, surface)) / lightSample.solidAnglePdf;
}

float RAB_LightSampleSelectionPdf(RAB_LightSample lightSample, RAB_Surface surface)
{
    if (lightSample.lightType == RAB_LIGHT_TYPE_DIRECTIONAL || lightSample.lightType == RAB_LIGHT_TYPE_SKY)
        return 1.0f;

    Instance instance = Instances[NonUniformResourceIndex(surface.instanceIndex)];
    return 1.0f / max(float(instance.LightData.Count), 1.0f);
}

bool RAB_GetConservativeVisibility(RAB_Surface rab, RAB_LightSample lightSample)
{
    return RAB_GetConservativeVisibility(rab, RAB_LightSamplePosition(lightSample));
}

float3 RAB_ApplyLightVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    uint randomSeed = PCGHash(
        asuint(surface.surface.Position.x) ^
        asuint(surface.surface.Position.y) ^
        asuint(surface.surface.Position.z) ^
        lightSample.lightIndex);

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);

    if (lightSample.lightType == RAB_LIGHT_TYPE_DIRECTIONAL || lightSample.lightType == RAB_LIGHT_TYPE_SKY)
        return TraceRayShadow(Scene, surface.surface, lightDir, randomSeed);

    return TraceRayShadowFinite(Scene, surface.surface, lightDir, lightDistance, randomSeed);
}

uint RAB_PackDirectionalLightIndex() { return 0; }
uint RAB_PackSkyLightIndex() { return 1; }
uint RAB_PackPointLightIndex(uint lightIndex) { return lightIndex + 2; }
bool RAB_IsDirectionalLightIndex(uint lightIndex) { return lightIndex == 0; }
bool RAB_IsSkyLightIndex(uint lightIndex) { return lightIndex == 1; }
uint RAB_UnpackPointLightIndex(uint lightIndex) { return lightIndex - 2; }

float3 RAB_SampleSkyDirection(float2 uv)
{
    float z = uv.x;
    float r = sqrt(max(0.0f, 1.0f - z * z));
    float phi = 2.0f * K_PI * uv.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

float RAB_EvaluateSkySamplingPdf(float3 direction)
{
    return direction.z > 0.0f ? rcp(2.0f * K_PI) : 0.0f;
}

RAB_LightInfo RAB_LoadLightInfo(uint lightIndex, bool isPrevFrame)
{
    RAB_LightInfo info;
    if (RAB_IsDirectionalLightIndex(lightIndex))
    {
        info.lightType = RAB_LIGHT_TYPE_DIRECTIONAL;
        info.lightIndex = 0;
    }
    else if (RAB_IsSkyLightIndex(lightIndex))
    {
        info.lightType = RAB_LIGHT_TYPE_SKY;
        info.lightIndex = 0;
    }
    else
    {
        info.lightType = RAB_LIGHT_TYPE_POINT;
        info.lightIndex = RAB_UnpackPointLightIndex(lightIndex);
    }
    return info;
}

RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    RAB_LightSample ls = RAB_EmptyLightSample();
    ls.lightType = lightInfo.lightType;
    ls.lightIndex = lightInfo.lightIndex;

    if (lightInfo.lightType == RAB_LIGHT_TYPE_DIRECTIONAL)
    {
        float3 irradiance = DirLightToLinear(Raytracing.DirectionalLight.Color) *
            EvalSkyOcclusion(SkyHemisphere, Raytracing.DirectionalLight.Vector, Features.CloudShadows.Opacity);
        float3 lightDir = normalize(Raytracing.DirectionalLight.Vector);
        ls.position = surface.surface.Position + lightDir * SHADOW_RAY_TMAX;
        ls.distance = SHADOW_RAY_TMAX;
        ls.solidAnglePdf = 1.0f;
        ls.radiance = irradiance * Raytracing.Directional;
        return ls;
    }

    if (lightInfo.lightType == RAB_LIGHT_TYPE_SKY)
    {
        float3 lightDir = RAB_SampleSkyDirection(uv);
        ls.position = surface.surface.Position + lightDir * RAB_DISTANT_LIGHT_DISTANCE;
        ls.distance = RAB_DISTANT_LIGHT_DISTANCE;
        ls.solidAnglePdf = RAB_EvaluateSkySamplingPdf(lightDir);
        ls.radiance = SampleSky(SkyHemisphere, lightDir) * Raytracing.Sky;
        return ls;
    }

    Light light = Lights[NonUniformResourceIndex(lightInfo.lightIndex)];
    const bool isLinear = (light.Flags & LightFlags::LinearLight) != 0;
    light.Color = PointLightToLinear(light.Color, isLinear);

    float3 toLight = light.Vector - surface.surface.Position;
    float dist = length(toLight);
    float3 lightDir = toLight / max(dist, 1e-4f);
    float lightSourceAngle = 0.005f;
    float atten = GetAttenuation(light, dist, lightSourceAngle) * GetSpotAttenuation(light, lightDir);

    ls.position = light.Vector;
    ls.distance = dist;
    ls.solidAnglePdf = 1.0f;
    ls.radiance = light.Color * light.Fade * atten * Raytracing.Point;
    return ls;
}

int RAB_TranslateLightIndex(uint lightIndex, bool prevToCurrent) { return (int)lightIndex; }

float3 RAB_GetMISWeightForNEE(uint lightIndex, RAB_LightSample lightSample, float3 lightDir, float lightPdf, float scatterPdf)
{
    if (RAB_IsAnalyticLightSample(lightSample))
        return float3(1.0f, 1.0f, 1.0f);

    const float neePdf = max(lightPdf, 0.0f);
    float misWeight = neePdf > 0.0f ? neePdf / (neePdf + max(scatterPdf, 0.0f)) : 0.0f;
    return float3(misWeight, misWeight, misWeight);
}

float3 RAB_GetMISWeightForEmissiveSurface(float3 radiance, float brdfPdf, float emissivePdf)
{
    float misWeight = (brdfPdf > 0) ? brdfPdf / (brdfPdf + emissivePdf) : 0.0;
    return float3(misWeight, misWeight, misWeight);
}

float3 RAB_GetMISWeightForEnvironmentMap(float3 radiance, float brdfPdf, float envPdf)
{
    float misWeight = (brdfPdf > 0.0f) ? brdfPdf / (brdfPdf + max(envPdf, 0.0f)) : 0.0f;
    return float3(misWeight, misWeight, misWeight);
}

// ---------------------------------------------------------------------------
// Ray payload for PT context
// ---------------------------------------------------------------------------
struct RAB_RayPayload
{
    float hitT;
    bool hit;
};

float RAB_RayPayloadGetCommittedHitT(RAB_RayPayload rp) { return rp.hitT; }
bool RAB_RayPayloadHit(RAB_RayPayload rp) { return rp.hit; }

RAB_RayPayload RAB_MakeRayPayload(Payload payload)
{
    RAB_RayPayload rp;
    rp.hitT = payload.hitDistance;
    rp.hit = payload.Hit();
    return rp;
}

#endif // RTXDI_PT_APPLICATION_BRIDGE_HLSLI
