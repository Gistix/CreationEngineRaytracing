#ifndef RTXDI_APPLICATION_BRIDGE_HLSLI
#define RTXDI_APPLICATION_BRIDGE_HLSLI

// RAB (RTXDI Application Bridge) — follows RTXPT's architecture exactly:
//   Path tracer writes packed Surface data into a ping-pong StructuredBuffer.
//   ReSTIR GI reads it back and reconstructs Surface + BRDFContext for BRDF eval.
//   NO GBuffer reconstruction heuristics.

#include "Registers.hlsli"

// Material type required by StandardBSDF method signatures in BSDF.hlsli
#include "interop/Material.hlsli"

// Include advanced settings for DIFFUSE_MODE defaults
#include "raytracing/include/AdvancedSettings.hlsli"

// Full BSDF system
#include "raytracing/include/Materials/BSDF.hlsli"
#include "include/Common/Color.hlsli"
#include "raytracing/include/RayOffset.hlsli"

// RTXDI types
#include <Rtxdi/GI/Reservoir.hlsli>
#include <Rtxdi/Utils/RandomSamplerState.hlsli>

// ---------------------------------------------------------------------------
// RAB_Surface — reconstructed from PackedSurfaceData, holds full Surface + BRDFContext.
// Analogous to RTXPT's PathTracerCollectedSurfaceData with Eval() method.
// ---------------------------------------------------------------------------
struct RAB_Surface
{
    Surface surface;
    BRDFContext brdfContext;
    float viewDepth;

    // Evaluate BSDF: transforms wi/wo to local frame, creates DefaultBSDF, calls Eval.
    // Returns float4(diffuse+specular, specularAvg).
    float4 Eval(float3 wo)
    {
        float3 wi = brdfContext.ViewDirection;
        float3 wiLocal = surface.ToLocal(wi);
        float3 woLocal = surface.ToLocal(wo);
        DefaultBSDF bsdf = DefaultBSDF::make(surface.Normal, wi, surface, true);
        return bsdf.Eval(wiLocal, woLocal);
    }

    // Evaluate BSDF with roughness clamped to a minimum value.
    // Used for roughness-aware MIS weight computation in final shading.
    float4 EvalRoughnessClamp(float minRoughness, float3 wo)
    {
        Surface roughSurface = surface;
        roughSurface.Roughness = max(roughSurface.Roughness, minRoughness);
        float3 wi = brdfContext.ViewDirection;
        float3 wiLocal = roughSurface.ToLocal(wi);
        float3 woLocal = roughSurface.ToLocal(wo);
        DefaultBSDF bsdf = DefaultBSDF::make(roughSurface.Normal, wi, roughSurface, true);
        return bsdf.Eval(wiLocal, woLocal);
    }
};

static const float RAB_BACKGROUND_DEPTH = 1e4f;

// ---------------------------------------------------------------------------
// Unpack PackedSurfaceData -> full Surface
// ---------------------------------------------------------------------------
Surface PSD_UnpackToSurface(PackedSurfaceData d)
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

    // Compute derived fields
    float maxF0 = max(max(s.F0.r, s.F0.g), s.F0.b);
    s.IOR = (1.0 + sqrt(maxF0)) / max(1.0 - sqrt(maxF0), 1e-5);

    // Defaults for properties not stored
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
// Load packed surface from ping-pong buffer — analogous to RTXPT's getGBufferSurface()
// ---------------------------------------------------------------------------
RAB_Surface LoadSurfaceFromBuffer(uint2 pixelPosition, bool previousFrame)
{
    RAB_Surface rab;

    // Ping-pong: current frame = FrameIndex%2, previous = (FrameIndex+1)%2
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
        rab.viewDepth = RAB_BACKGROUND_DEPTH;
        return rab;
    }

    rab.surface = PSD_UnpackToSurface(packed);
    float3 viewDir = PSD_UnpackOct(packed.packedViewDir);
    rab.brdfContext = BRDFContext::make(rab.surface, viewDir);
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

float3 RAB_GetSurfaceWorldPos(RAB_Surface rab)
{
    return rab.surface.Position;
}

// ---------------------------------------------------------------------------
// Read surface data from packed buffer — replaces GBuffer reconstruction
// ---------------------------------------------------------------------------
RAB_Surface RAB_GetGBufferSurface(uint2 pixelPosition, bool previousFrame)
{
    if (any(pixelPosition >= Camera.RenderSize))
        return RAB_EmptySurface();

    return LoadSurfaceFromBuffer(pixelPosition, previousFrame);
}

// ---------------------------------------------------------------------------
// RAB_Material and similarity check (following RTXPT thresholds)
// ---------------------------------------------------------------------------
typedef RAB_Surface RAB_Material;

RAB_Material RAB_GetMaterial(RAB_Surface rab)
{
    return rab;
}

bool RAB_AreMaterialsSimilar(RAB_Material a, RAB_Material b)
{
    if (abs(a.surface.Roughness - b.surface.Roughness) > 0.5)
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

// ---------------------------------------------------------------------------
// Target PDF through BSDF evaluation — following RTXPT exactly:
//   float3 reflectedRadiance = surface.Eval(L).rgb * sampleRadiance;
//   return Luminance(reflectedRadiance);
// ---------------------------------------------------------------------------
float RAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface rab)
{
    float3 L = normalize(samplePosition - rab.surface.Position);

    float4 bsdfEval = rab.Eval(L);
    float3 reflectedRadiance = bsdfEval.rgb * sampleRadiance;

    return max(0, Color::RGBToLuminance(reflectedRadiance));
}

// ---------------------------------------------------------------------------
// Jacobian validation (following RTXPT: reject > 10 or < 1/10, clamp to [1/3, 3])
// ---------------------------------------------------------------------------
bool RAB_ValidateGISampleWithJacobian(inout float jacobian)
{
    if (jacobian > 10.0 || jacobian < 1.0 / 10.0)
        return false;

    jacobian = clamp(jacobian, 1.0 / 3.0, 3.0);
    return true;
}

// ---------------------------------------------------------------------------
// Visibility ray setup following RTXPT's pattern
// ---------------------------------------------------------------------------
bool RAB_GetConservativeVisibility(RAB_Surface rab, float3 samplePosition)
{
    float3 toSample = samplePosition - rab.surface.Position;
    float dist = length(toSample);

    if (dist < 1e-4)
        return true;

    float3 origin = OffsetRay(rab.surface.Position, rab.surface.FaceNormal);

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

#endif // RTXDI_APPLICATION_BRIDGE_HLSLI
