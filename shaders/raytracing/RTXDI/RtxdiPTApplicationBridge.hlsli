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
#include "include/Common/Color.hlsli"
#include "raytracing/include/RayOffset.hlsli"

// RTXDI types
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
    float viewDepth;

    float4 Eval(float3 wo)
    {
        float3 wi = brdfContext.ViewDirection;
        float3 wiLocal = surface.ToLocal(wi);
        float3 woLocal = surface.ToLocal(wo);
        DefaultBSDF bsdf = DefaultBSDF::make(surface.Normal, wi, surface, true);
        return bsdf.Eval(wiLocal, woLocal);
    }

    float EvalPdf(float3 wo)
    {
        float3 woLocal = surface.ToLocal(wo);
        float3 wiLocal = surface.ToLocal(brdfContext.ViewDirection);
        DefaultBSDF bsdf = DefaultBSDF::make(surface.Normal, brdfContext.ViewDirection, surface, true);
        return bsdf.Pdf(wiLocal, woLocal);
    }
};

static const float RAB_BACKGROUND_DEPTH = 1e4f;

// ---------------------------------------------------------------------------
// Unpack PackedSurfaceData -> full Surface
// ---------------------------------------------------------------------------
Surface PSD_UnpackToSurfacePT(PackedSurfaceData d)
{
    Surface s;
    s.Primary      = true;
    s.Position     = d.posW;
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
    s.IsThinSurface      = false;
    s.CoatColor          = 1;
    s.CoatStrength       = 0;
    s.CoatRoughness      = 0;
    s.CoatF0             = 0.04;
    s.CoatNormal         = s.Normal;
    s.CoatTangent        = s.Tangent;
    s.CoatBitangent      = s.Bitangent;
    s.MipLevel           = 0;

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
        rab.viewDepth = RAB_BACKGROUND_DEPTH;
        return rab;
    }

    rab.surface = PSD_UnpackToSurfacePT(packed);
    float3 viewDir = PSD_UnpackOct(packed.packedViewDir);
    rab.brdfContext = BRDFContext::make(rab.surface, viewDir);
    rab.materialFeature = PSD_GetMaterialFeature(packed);
    rab.hasSpecularTransmission = PSD_SurfaceHasSpecularTransmission(packed);
    rab.viewDepth = packed.viewDepth;

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
    rab.viewDepth = RAB_BACKGROUND_DEPTH;
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

    if (a.hasSpecularTransmission != b.hasSpecularTransmission)
        return false;

    if (abs(a.surface.Roughness - b.surface.Roughness) > 0.5)
        return false;

    float reflA = Color::RGBToLuminance(a.surface.F0);
    float reflB = Color::RGBToLuminance(b.surface.F0);
    if (abs(reflA - reflB) > 0.25)
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Target PDF through BSDF evaluation for PT
// ---------------------------------------------------------------------------
float3 RAB_GetPTSampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface rab)
{
    float3 L = normalize(samplePosition - rab.surface.Position);
    float4 bsdfEval = rab.Eval(L);
    return bsdfEval.rgb * sampleRadiance;
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
    float4 bsdfEval = rab.Eval(L);
    return bsdfEval.rgb * lightRadiance;
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

    bool behindSurface = dot(toSample, rab.surface.FaceNormal) < 0;
    float3 origin = OffsetRayAlt(rab.surface.Position, rab.surface.FaceNormal, behindSurface);

    float3 offsetToSample = samplePosition - origin;
    float offsetDist = length(offsetToSample);

    if (offsetDist < 1e-4)
        return true;

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = offsetToSample / offsetDist;
    ray.TMin      = 0.0;
    ray.TMax      = max(0.0, offsetDist - 0.001);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> rayQuery;
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_CULL_NON_OPAQUE, 0xFF, ray);
    rayQuery.Proceed();

    return rayQuery.CommittedStatus() == COMMITTED_NOTHING;
}

bool RAB_GetConservativeVisibility(RAB_Surface rab, RAB_LightSample lightSample)
{
    return RAB_GetConservativeVisibility(rab, RAB_LightSamplePosition(lightSample));
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
};

void RAB_PathTracerUserDataSetPathType(inout RAB_PathTracerUserData ptud, uint pathType)
{
    ptud.pathType = pathType;
}

// ---------------------------------------------------------------------------
// Denoiser callbacks (no-ops for now; PSR not implemented in initial version)
// ---------------------------------------------------------------------------
void RAB_ReconnectionDenoiserCallback(RTXDI_PTReservoir reservoir, RAB_Surface surface, inout RAB_PathTracerUserData ptud)
{
    // No-op: denoiser guide buffers not affected by reconnection in this engine
}

void RAB_LastBounceDenoiserCallback(float3 lightPos, RAB_Surface surface, inout RAB_PathTracerUserData ptud)
{
    // No-op
}

// ---------------------------------------------------------------------------
// Light system stubs for NEE reconnection
// ReSTIR PT requires these for ConnectsToNEELight paths.
// In this project, lights are NOT polymorphic RTXDI lights, so NEE reconnection
// is initially disabled. These stubs make the library compile; actual
// light bridge can be implemented in a future pass.
// ---------------------------------------------------------------------------
struct RAB_LightInfo { uint dummy; };
struct RAB_LightSample { float3 position; float3 radiance; float solidAnglePdf; };

RAB_LightInfo RAB_EmptyLightInfo() { RAB_LightInfo li; li.dummy = 0; return li; }
RAB_LightSample RAB_EmptyLightSample() { RAB_LightSample ls; ls.position = 0; ls.radiance = 0; ls.solidAnglePdf = 0; return ls; }

float3 RAB_LightSamplePosition(RAB_LightSample ls) { return ls.position; }
float3 RAB_LightSampleRadiance(RAB_LightSample ls) { return ls.radiance; }
float RAB_LightSampleSolidAnglePdf(RAB_LightSample ls) { return ls.solidAnglePdf; }

RAB_LightInfo RAB_LoadLightInfo(uint lightIndex, bool isPrevFrame) { return RAB_EmptyLightInfo(); }
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv) { return RAB_EmptyLightSample(); }

int RAB_TranslateLightIndex(uint lightIndex, bool prevToCurrent) { return (int)lightIndex; }

float3 RAB_GetMISWeightForNEE(uint lightIndex, RAB_LightSample lightSample, float3 lightDir, float lightPdf, float scatterPdf)
{
    // Balance heuristic between NEE and BSDF sampling
    float misWeight = (lightPdf > 0) ? lightPdf / (lightPdf + scatterPdf) : 0.0;
    return float3(misWeight, misWeight, misWeight);
}

float3 RAB_GetMISWeightForEmissiveSurface(float3 radiance, float brdfPdf, float emissivePdf)
{
    float misWeight = (brdfPdf > 0) ? brdfPdf / (brdfPdf + emissivePdf) : 0.0;
    return float3(misWeight, misWeight, misWeight);
}

float3 RAB_GetMISWeightForEnvironmentMap(float3 radiance, float brdfPdf, float envPdf)
{
    return float3(1.0, 1.0, 1.0); // No environment map sampling in this engine
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

#endif // RTXDI_PT_APPLICATION_BRIDGE_HLSLI
