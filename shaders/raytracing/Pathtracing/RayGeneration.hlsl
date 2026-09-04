#if !(defined(SHARC) && SHARC_UPDATE) && DEBUG_TRACE_HEATMAP
#   ifndef NV_HLSL_EXTNS_INCLUDED
#       define NV_HLSL_EXTNS_INCLUDED 1
#       define NV_SHADER_EXTN_SLOT u127
#       define NV_SHADER_EXTN_REGISTER_SPACE space0
#       include "include/nvapi/nvHLSLExtns.h"
#   endif

#   include "include/nvapi/Profiling.hlsli"
#endif

#include "raytracing/Pathtracing/Registers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Geometry.hlsli"

#include "raytracing/include/Materials/TexLODHelpers.hlsli"

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

#include "include/Lighting.hlsli"

#if defined(SUBSURFACE_SCATTERING)
#include "raytracing/include/SubsurfaceLighting.hlsli"
#endif

#include "raytracing/include/Transparency.hlsli"

#include "Raytracing/Include/SHARC/Sharc.hlsli"
#include "Raytracing/Include/SHARC/SHaRCHelper.hlsli"

static const float kEnvironmentMapSceneDistance = 50000.0f;

#if defined(PSR)
// ============================================================================
// Primary Surface Replacement (PSR) Helpers
// ============================================================================

float Average3(float3 rgb) { return (rgb.x + rgb.y + rgb.z) / 3.0; }

float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * select(v.xy >= 0.0, 1.0.xx, -1.0.xx);
}

float2 OctEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

uint NDirToOctUnorm32(float3 n)
{
    float2 p = OctEncode(n);
    return uint(saturate(p.x) * 0xFFFE) | (uint(saturate(p.y) * 0xFFFE) << 16);
}

float3 OctToNDirUnorm32(uint pUnorm)
{
    float2 p;
    p.x = saturate(float(pUnorm & 0xFFFF) / float(0xFFFE));
    p.y = saturate(float(pUnorm >> 16) / float(0xFFFE));
    p = p * 2.0 - 1.0;
    float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t.xx, t.xx);
    return normalize(n);
}

float3x3 MatrixRotateFromTo(float3 from, float3 to)
{
    float3 v = cross(from, to);
    float c = dot(from, to);
    float k = 1.0 / (1.0 + c + 1e-7);
    return float3x3(
        v.x * v.x * k + c,     v.x * v.y * k - v.z,   v.x * v.z * k + v.y,
        v.y * v.x * k + v.z,   v.y * v.y * k + c,      v.y * v.z * k - v.x,
        v.z * v.x * k - v.y,   v.z * v.y * k + v.x,    v.z * v.z * k + c
    );
}

float3x3 MirrorReflectionMatrix(float3 n)
{
    return float3x3(
        1.0 - 2.0*n.x*n.x,  -2.0*n.x*n.y,       -2.0*n.x*n.z,
        -2.0*n.y*n.x,         1.0 - 2.0*n.y*n.y,  -2.0*n.y*n.z,
        -2.0*n.z*n.x,        -2.0*n.z*n.y,         1.0 - 2.0*n.z*n.z
    );
}

float3x3 UpdateImageXform(float3x3 prevXform, float3 inDir, float3 outDir, float3 normal, bool isTransmission)
{
    float3x3 bounceMatrix;
    if (isTransmission)
    {
        bounceMatrix = MatrixRotateFromTo(-inDir, outDir);
    }
    else
    {
        bounceMatrix = MirrorReflectionMatrix(normal);
    }
    return mul(bounceMatrix, prevXform);
}

void computePSRMotionVectorsAndDepth(
    const uint2 pixelPos,
    const float totalSceneLength,
    const float3x3 imageXform,
    const float3 surfaceCameraPosition,
    const float3 surfacePrevCameraPosition,
    out float3 outMotionVectors,
    out float outDepth)
{
    float3 cameraRayDir = normalize(mul((float3x3)Camera.ViewInverse,
        GetView(pixelPos, Camera.RenderSize, Camera.ProjInverse)));
    float3 virtualCameraPos = cameraRayDir * totalSceneLength;
    float3 cameraDelta = Camera.Position - Camera.PositionPrev;
    float3 worldMotion = surfacePrevCameraPosition - surfaceCameraPosition - cameraDelta;
    float3 virtualMotion = mul(imageXform, worldMotion);
    outMotionVectors = computeMotionVectorCameraRelative(virtualCameraPos, virtualCameraPos + virtualMotion + cameraDelta);
    outDepth = computeClipDepthCameraRelative(virtualCameraPos);
}
#endif

#if defined(GROUP_TILING)
#   define DXC_STATIC_DISPATCH_GRID_DIM 1
#   include "include/ThreadGroupTilingX.hlsli"
#endif

#include "include/NRD.hlsli"

#if USE_RAY_QUERY
WAVE_SIZE(32)
[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
#   if defined(GROUP_TILING)
void Main(uint2 GTid : SV_GroupThreadID, uint2 Gid : SV_GroupID)
#   else
void Main(uint2 idx : SV_DispatchThreadID)
#   endif
#else
[shader("raygeneration")]
void Main()
#endif
{
#if USE_RAY_QUERY
    uint2 size = Camera.RenderSize;  
#   if defined(GROUP_TILING)    
    uint2 idx = ThreadGroupTilingX((uint2)ceil(size / THREAD_GROUP_SIZE), THREAD_GROUP_SIZE.xx, 32, GTid.xy, Gid.xy);
#   endif
    if (any(idx >= size))
        return;
#else    
    uint2 idx = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
#endif

#if defined(SHARC)
    SharcParameters sharcParameters = GetSharcParameters();

#    if SHARC_UPDATE
        uint startIndex = Hash(idx) % 25;

        uint2 blockOrigin = idx * 5;

        uint pixelIndex = (startIndex + Camera.FrameIndex) % 25;

        idx = blockOrigin + uint2(pixelIndex % 5, pixelIndex / 5);

        if (any(idx >= Camera.RenderSize))
            return;

        size = Camera.RenderSize;
#   endif

#endif    

    // ReSTIR GI: Write empty packed surface (overwritten below on valid hit).
#if defined(RESTIR_GI)    
#   if !(defined(SHARC) && SHARC_UPDATE) && !defined(PSR)
    uint surfBufIdx = (Camera.FrameIndex % 2) * (size.x * size.y) + idx.y * size.x + idx.x;
    SurfaceDataBuffer[surfBufIdx] = PSD_Empty();
#   endif
#endif
    
    RayDesc sourceRay = SetupPrimaryRay(idx, size, Camera);
    
    float3 sourceDirection = sourceRay.Direction;
    
    uint randomSeed = InitRandomSeed(idx, size, Camera.FrameIndex);
    
#if !(defined(SHARC) && SHARC_UPDATE) && DEBUG_TRACE_HEATMAP       
    uint startTime = NvGetSpecial( NV_SPECIALOP_GLOBAL_TIMER_LO );
#endif
    
    Payload sourcePayload = TraceRayStandard(Scene, sourceRay, randomSeed, true);

#if !(defined(SHARC) && SHARC_UPDATE) && DEBUG_TRACE_HEATMAP       
    uint endTime = NvGetSpecial( NV_SPECIALOP_GLOBAL_TIMER_LO );
    uint deltaTime = timediff(startTime, endTime);
    
    // Scale the time delta value to [0,1]
    static float heatmapScale = 300000.0f; // somewhat arbitrary scaling factor, experiment to find a value that works well in your app 
    float deltaTimeScaled =  clamp( (float)deltaTime / heatmapScale, 0.0f, 1.0f );

    // Compute the heatmap color and write it to the output pixel
    Output[idx] = float4(temperature(deltaTimeScaled), 1.0f);     
    
    return;
 #endif

    if (!sourcePayload.Hit())
    {
#if !(defined(SHARC) && SHARC_UPDATE)
        float3 skyRad = SampleSky(SkyHemisphere, sourceDirection) * Raytracing.Sky;
        if (Camera.IsUnderwater != 0 && any(Camera.UnderwaterAbsorption > 0.0f))
            skyRad *= exp(-Camera.UnderwaterAbsorption * kEnvironmentMapSceneDistance);

        Output[idx] = float4(LLTrueLinearToGamma(skyRad), 1.0f);
        
#   if defined(NRD) | defined(DLSS_RR)
        DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f); 
        
#       if defined(NRD)
        DiffuseRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(0.0f, 0.0f, false);
        SpecularRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(skyRad, 0.0f, false);          
#       else
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);
        SpecularHitDistance[idx] = 0;                
#       endif              
#   endif       
        
        NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);

        float3 skyVirtualPos = sourceDirection * kEnvironmentMapSceneDistance;
        MotionVectors[idx] = float4(computeMotionVectorCameraRelative(
            skyVirtualPos,
            skyVirtualPos + (Camera.Position - Camera.PositionPrev)), 0);
        Depth[idx] = 1;  // sky → far plane (standard Z: 0=near, 1=far)    
#   if defined(NRD) 
        ViewDepth[idx] = ScreenToViewDepth(1.0f, Camera.CameraData);
#   endif        
#endif
#if defined(PSR)
        PSR_RaySegment[idx] = float4(kEnvironmentMapSceneDistance, 0.0f, kEnvironmentMapSceneDistance, 0.0f);
        PSR_Throughput[idx] = float4(1.0f, 1.0f, 1.0f, 0.0f);
#endif
        return;
    }
          
    RayCone sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * sourcePayload.hitDistance, Raytracing.PixelConeSpreadAngle);   
    
    float3 sourcePosition = Camera.Position.xyz + sourceDirection * sourcePayload.hitDistance;
    
    Instance sourceInstance;
    LightingMaterialData sourceMaterial;

    Surface sourceSurface = SurfaceMaker::make(sourcePosition, sourcePayload, sourceDirection, sourceRayCone, sourceInstance, sourceMaterial, true);

    // Pass through Effect materials on primary ray: accumulate emissive, don't interact
    float3 primaryEffectEmissive = float3(0, 0, 0);
#if defined(EFFECT_PASSTHROUGH)    
    [loop]
    for (uint effectPrimPass = 0; effectPrimPass < 16 && sourceMaterial.Type == Type::Effect; effectPrimPass++)
    {
        primaryEffectEmissive += sourceSurface.Emissive;

        float3 fn = dot(sourceDirection, sourceSurface.FaceNormal) <= 0.0f ? sourceSurface.FaceNormal : -sourceSurface.FaceNormal;
#if USE_SIA_INTERPOLATION
        sourceRay.Origin = OffsetRaySIA(sourceSurface.Position, fn, sourceSurface.SIAOffset, true);
#else
        sourceRay.Origin = OffsetRay(sourceSurface.Position, fn, sourceSurface.PositionError, true);
#endif
        sourceRay.Direction = sourceDirection;
        sourceRay.TMin = 0.0f;
        sourceRay.TMax = RAY_TMAX;

        sourcePayload = TraceRayStandard(Scene, sourceRay, randomSeed);

        if (!sourcePayload.Hit())
            break;

        sourcePosition = sourceRay.Origin + sourceDirection * sourcePayload.hitDistance;
        sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * length(sourcePosition - Camera.Position.xyz), Raytracing.PixelConeSpreadAngle);
        sourceSurface = SurfaceMaker::make(sourcePosition, sourcePayload, sourceDirection, sourceRayCone, sourceInstance, sourceMaterial, true);
    }

    // Effect pass-through ended in sky miss
    if (!sourcePayload.Hit())
    {
        float3 skyRadiance = SampleSky(SkyHemisphere, sourceDirection) * Raytracing.Sky + primaryEffectEmissive;
        if (Camera.IsUnderwater != 0 && any(Camera.UnderwaterAbsorption > 0.0f))
            skyRadiance *= exp(-Camera.UnderwaterAbsorption * kEnvironmentMapSceneDistance);

#if !(defined(SHARC) && SHARC_UPDATE)
        Output[idx] = float4(LLTrueLinearToGamma(skyRadiance), 1.0f);
        NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        
        float3 skyVirtualPos = sourceDirection * kEnvironmentMapSceneDistance;
        MotionVectors[idx] = float4(computeMotionVectorCameraRelative(
            skyVirtualPos,
            skyVirtualPos + (Camera.Position - Camera.PositionPrev)), 0);
        Depth[idx] = 1;
    
#   if defined(NRD) | defined(DLSS_RR)   
        DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f);
#       if defined(NRD) 
        ViewDepth[idx] = ScreenToViewDepth(1.0f, Camera.CameraData);
        DiffuseRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(0.0f, 0.0f, false);
        SpecularRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(skyRadiance, 0.0f, false);
#       else
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);
        SpecularHitDistance[idx] = 0;
#       endif  
#   endif
#endif
#if defined(PSR)
        PSR_RaySegment[idx] = float4(kEnvironmentMapSceneDistance, 0.0f, kEnvironmentMapSceneDistance, 0.0f);
        PSR_Throughput[idx] = float4(1.0f, 1.0f, 1.0f, 0.0f);
#endif
        return;
    }
#endif
    
    float primarySceneDistance = length(sourcePosition - Camera.Position.xyz);

    BRDFContext sourceBRDFContext = BRDFContext::make(sourceSurface, -sourceDirection);

    bool sourceIsEnter = dot(sourceSurface.FaceNormal, sourceBRDFContext.ViewDirection) >= 0.0f;
    if (!sourceIsEnter) {
        sourceSurface.FlipNormal();
        sourceBRDFContext.NdotV = saturate(dot(sourceSurface.Normal, sourceBRDFContext.ViewDirection));
    }

    AdjustShadingNormal(sourceSurface, sourceBRDFContext, true, false);    

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, sourceSurface.Normal, sourceBRDFContext.ViewDirection, sourceIsEnter);
    
 #if !(defined(SHARC) && SHARC_UPDATE)
    // Coat-priority GBuffer: when coat is present, use coat normal/roughness for denoiser;
    // base diffuse is tinted by coat transmission (semi-transparent coat lets base color through).
    const bool useCoat = sourceSurface.CoatStrength > 0;
  
#   if defined(NRD) | defined(DLSS_RR)    
    const float3 coatTint = lerp(float3(1, 1, 1), sourceSurface.CoatColor, sourceSurface.CoatStrength);
    DiffuseAlbedo[idx] = sourceSurface.DiffuseAlbedo * coatTint;
#   endif   
    
#   if defined(DLSS_RR)    
    if (useCoat)
    {
        float coatNdotV = saturate(dot(sourceSurface.CoatNormal, sourceBRDFContext.ViewDirection));
        const float2 envBRDF = BRDF::EnvBRDF(sourceSurface.CoatRoughness, coatNdotV);
        SpecularAlbedo[idx] = float3(sourceSurface.CoatF0 * envBRDF.x + envBRDF.y);
    }
    else
    {
        const float2 envBRDF = BRDF::EnvBRDF(sourceSurface.Roughness, sourceBRDFContext.NdotV);
        SpecularAlbedo[idx] = float3(sourceSurface.F0 * envBRDF.x + envBRDF.y);
    }
#   endif
#if defined(NRD)
    NormalRoughness[idx] = NRD_FrontEnd_PackNormalAndRoughness(
        useCoat ? sourceSurface.CoatNormal : sourceSurface.Normal, 
        useCoat ? sourceSurface.CoatRoughness : sourceSurface.Roughness, 
        0.0f
    );
#else
    NormalRoughness[idx] = float4(
        normalize(useCoat ? sourceSurface.CoatNormal : sourceSurface.Normal), 
        saturate(useCoat ? sourceSurface.CoatRoughness : sourceSurface.Roughness)
    );
#endif

#if !defined(PSR)
    MotionVectors[idx] = float4(computeMotionVectorCameraRelative(
        sourceSurface.CameraRelativePosition,
        sourceSurface.PrevCameraRelativePosition), 0);
    
    const float depth = computeClipDepthCameraRelative(sourceSurface.CameraRelativePosition);
    Depth[idx] = depth;
    
#   if defined(NRD) 
    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
    ViewDepth[idx] = depthVS;
#   endif
    
#   if defined(RESTIR_GI)    
    // Write packed surface data for ReSTIR GI
    SurfaceDataBuffer[surfBufIdx] = PSD_Pack(
        sourceSurface.Position, sourceSurface.Normal, sourceSurface.Tangent, sourceSurface.Bitangent,
        sourceSurface.FaceNormal, sourceBRDFContext.ViewDirection,
        sourceSurface.DiffuseAlbedo, sourceSurface.F0,
        sourceSurface.Roughness, sourceSurface.Metallic,
        sourceMaterial.Feature, sourceSurface.SpecTrans > 0.0f,
        primarySceneDistance);
#   endif  
#endif
#endif

    float3 psrPathThroughput = float3(1.0f, 1.0f, 1.0f);
    bool pathInsideWaterVolume = Camera.IsUnderwater != 0;
    float3 pathWaterVolumeAbsorption = pathInsideWaterVolume ? Camera.UnderwaterAbsorption : float3(0.0f, 0.0f, 0.0f);

#if defined(PSR)
    {
        DeltaLobe deltaLobes[cMaxDeltaLobes];
        int deltaLobeCount;
        float nonDeltaPart;
        sourceBSDF.EvalDeltaLobes(sourceBRDFContext, sourceSurface, deltaLobes, deltaLobeCount, nonDeltaPart);

        bool isInternalSpecularTransmission = sourceSurface.SpecTrans > 0.0f && !sourceSurface.IsThinSurface && !sourceIsEnter;
        for (int k = 0; k < deltaLobeCount; k++)
        {
            if (isInternalSpecularTransmission && deltaLobes[k].transmission == 0)
                deltaLobes[k].thp = 0;

            if (Average3(abs(deltaLobes[k].thp)) < 0.001)
                deltaLobes[k].thp = 0;
        }

        int activeDeltaLobes = 0;
        int firstActiveLobe = -1;
        for (int k2 = 0; k2 < deltaLobeCount; k2++)
        {
            if (any(deltaLobes[k2].thp > 0))
            {
                if (firstActiveLobe < 0) firstActiveLobe = k2;
                activeDeltaLobes++;
            }
        }

        bool insideWaterVolume = pathInsideWaterVolume;
        float3 waterVolumeAbsorption = pathWaterVolumeAbsorption;

        // For transparent surfaces (e.g. water, glass), prioritize the transmission lobe
        // so that PSR follows the refracted ray down to the underlying surface (e.g. riverbed).
        if (sourceMaterial.Type == Type::Water || sourceSurface.SpecTrans > 0.0f)
        {
            for (int k3 = 0; k3 < deltaLobeCount; k3++)
            {
                if (any(deltaLobes[k3].thp > 0) && deltaLobes[k3].transmission != 0)
                {
                    firstActiveLobe = k3;
                    break;
                }
            }
        }

        const bool isDeltaSurface = (activeDeltaLobes > 0) && (nonDeltaPart <= 1e-5);

        if (!isDeltaSurface)
        {
            // Opaque primary surface: write standard primary G-buffers
            MotionVectors[idx] = float4(computeMotionVectorCameraRelative(
                sourceSurface.CameraRelativePosition,
                sourceSurface.PrevCameraRelativePosition), 0);
            
            const float depth = computeClipDepthCameraRelative(sourceSurface.CameraRelativePosition);
            Depth[idx] = depth;
            
#if defined(NRD) 
            ViewDepth[idx] = ScreenToViewDepth(depth, Camera.CameraData);
#endif
            PSR_RaySegment[idx] = float4(sourcePayload.hitDistance, 0.0f, sourcePayload.hitDistance, 0.0f);
            PSR_Throughput[idx] = float4(1.0f, 1.0f, 1.0f, 0.0f);
        }
        else
        {
            // Delta surface (mirror, glass, water): trace secondary ray and replace primary G-buffer
            DeltaLobe primaryLobe = deltaLobes[firstActiveLobe];
            float3 fn = dot(sourceBRDFContext.ViewDirection, sourceSurface.FaceNormal) >= 0.0 ? sourceSurface.FaceNormal : -sourceSurface.FaceNormal;

            RayDesc secRay;
#if USE_SIA_INTERPOLATION
            secRay.Origin = OffsetRaySIA(sourceSurface.Position, fn, sourceSurface.SIAOffset, primaryLobe.transmission != 0);
#else
            secRay.Origin = OffsetRay(sourceSurface.Position, fn, sourceSurface.PositionError, primaryLobe.transmission != 0);
#endif
            secRay.Direction = primaryLobe.dir;
            secRay.TMin = 0.0f;
            secRay.TMax = RAY_TMAX;

            float3x3 imageXform = float3x3(1,0,0, 0,1,0, 0,0,1);
            imageXform = UpdateImageXform(imageXform, sourceDirection, primaryLobe.dir, fn, primaryLobe.transmission != 0);

            float3 throughput = primaryLobe.thp;
            if (insideWaterVolume)
                throughput *= exp(-waterVolumeAbsorption * sourcePayload.hitDistance);

            if (primaryLobe.transmission != 0 && any(sourceSurface.VolumeAbsorption > 0.0f))
            {
                insideWaterVolume = sourceIsEnter;
                waterVolumeAbsorption = sourceIsEnter ? sourceSurface.VolumeAbsorption : float3(0, 0, 0);
            }

            float3 deltaLighting = EvalDeltaLobeLighting(sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed, true);

            Payload secPayload = TraceRayStandard(Scene, secRay, randomSeed);
            float d0 = sourcePayload.hitDistance;
            float d1 = secPayload.hitDistance;
            float totalSceneLength = d0 + d1;

            if (insideWaterVolume)
                throughput *= exp(-waterVolumeAbsorption * d1);

            if (!secPayload.Hit())
            {
                d1 = kEnvironmentMapSceneDistance;
                totalSceneLength = d0 + d1;
                float3 skyRad = SampleSky(SkyHemisphere, secRay.Direction) * Raytracing.Sky;
                if (insideWaterVolume && any(waterVolumeAbsorption > 0.0f))
                    skyRad *= exp(-waterVolumeAbsorption * kEnvironmentMapSceneDistance);

                float3 psrMV; float psrDepth;
                computePSRMotionVectorsAndDepth(idx, totalSceneLength, imageXform,
                    secRay.Direction * totalSceneLength, secRay.Direction * totalSceneLength,
                    psrMV, psrDepth);

                NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
                MotionVectors[idx] = float4(psrMV, 0);
                Depth[idx] = 1.0f;
#if defined(NRD) | defined(DLSS_RR)
                DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f);
#   if defined(NRD)
                ViewDepth[idx] = ScreenToViewDepth(1.0f, Camera.CameraData);
                DiffuseRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(0.0f, 0.0f, false);
                SpecularRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(deltaLighting + skyRad * throughput, 0.0f, false);
                DiffuseFactor[idx] = float3(0.0f, 0.0f, 0.0f);
                SpecularFactor[idx] = float3(1.0f, 1.0f, 1.0f);
#   else
                SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);
                SpecularHitDistance[idx] = 0.0f;
#   endif
#endif
#if !(defined(NRD))
                Output[idx] = float4(LLTrueLinearToGamma(deltaLighting + skyRad * throughput), 1.0f);
#endif

                PSR_RaySegment[idx] = float4(d0, kEnvironmentMapSceneDistance, totalSceneLength, asfloat(NDirToOctUnorm32(secRay.Direction)));
                PSR_Throughput[idx] = float4(throughput, 1.0f);
                return;
            }

            // Secondary hit S1
            float3 secPosition = secRay.Origin + secRay.Direction * secPayload.hitDistance;
            Instance secInstance;
            LightingMaterialData secMaterial;
            RayCone secRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * totalSceneLength, Raytracing.PixelConeSpreadAngle);
            Surface secSurface = SurfaceMaker::make(secPosition, secPayload, secRay.Direction, secRayCone, secInstance, secMaterial, true);
            BRDFContext secBRDFContext = BRDFContext::make(secSurface, -secRay.Direction);
            bool secIsEnter = dot(secSurface.FaceNormal, secBRDFContext.ViewDirection) >= 0.0f;
            if (!secIsEnter) {
                secSurface.FlipNormal();
                secBRDFContext.NdotV = saturate(dot(secSurface.Normal, secBRDFContext.ViewDirection));
            }
            AdjustShadingNormal(secSurface, secBRDFContext, true, false);

            float3 psrMV; float psrDepth;
            if (primaryLobe.transmission != 0)
            {
                psrMV = computeMotionVectorCameraRelative(
                    secSurface.CameraRelativePosition,
                    secSurface.PrevCameraRelativePosition);
                psrDepth = computeClipDepthCameraRelative(secSurface.CameraRelativePosition);
            }
            else
            {
                computePSRMotionVectorsAndDepth(idx, totalSceneLength, imageXform,
                    secSurface.CameraRelativePosition, secSurface.PrevCameraRelativePosition,
                    psrMV, psrDepth);
            }

            const bool secUseCoat = secSurface.CoatStrength > 0.0f;
            const float3 secCoatTint = lerp(float3(1, 1, 1), secSurface.CoatColor, secSurface.CoatStrength);

#if defined(NRD) | defined(DLSS_RR)
            DiffuseAlbedo[idx] = secSurface.DiffuseAlbedo * secCoatTint;
#   if defined(DLSS_RR)
            if (secUseCoat)
            {
                float coatNdotV = saturate(dot(secSurface.CoatNormal, secBRDFContext.ViewDirection));
                const float2 envBRDF = BRDF::EnvBRDF(secSurface.CoatRoughness, coatNdotV);
                SpecularAlbedo[idx] = float3(secSurface.CoatF0 * envBRDF.x + envBRDF.y);
            }
            else
            {
                const float2 envBRDF = BRDF::EnvBRDF(secSurface.Roughness, secBRDFContext.NdotV);
                SpecularAlbedo[idx] = float3(secSurface.F0 * envBRDF.x + envBRDF.y);
            }
#   endif
#endif

#if defined(NRD)
            NormalRoughness[idx] = NRD_FrontEnd_PackNormalAndRoughness(
                secUseCoat ? secSurface.CoatNormal : secSurface.Normal,
                secUseCoat ? secSurface.CoatRoughness : secSurface.Roughness,
                0.0f
            );
            ViewDepth[idx] = ScreenToViewDepth(psrDepth, Camera.CameraData);
#else
            NormalRoughness[idx] = float4(
                normalize(secUseCoat ? secSurface.CoatNormal : secSurface.Normal),
                saturate(secUseCoat ? secSurface.CoatRoughness : secSurface.Roughness)
            );
#endif

            MotionVectors[idx] = float4(psrMV, 0);
            Depth[idx] = psrDepth;

            PSR_RaySegment[idx] = float4(d0, d1, totalSceneLength, asfloat(NDirToOctUnorm32(secRay.Direction)));
            PSR_Throughput[idx] = float4(throughput, 1.0f);

            // Replace primary shading state with S1 so subsequent path tracing renders S1
            sourceSurface = secSurface;
            sourceBRDFContext = secBRDFContext;
            sourceBSDF = StandardBSDF::make(secSurface, secSurface.Normal, secBRDFContext.ViewDirection, secIsEnter);
            sourceDirection = secRay.Direction;
            sourcePayload = secPayload;
            sourceInstance = secInstance;
            sourceMaterial = secMaterial;
            sourceRayCone = secRayCone;
            sourceIsEnter = secIsEnter;
            primaryEffectEmissive += deltaLighting;
            psrPathThroughput = throughput;
            pathInsideWaterVolume = insideWaterVolume;
            pathWaterVolumeAbsorption = waterVolumeAbsorption;
        }
    }
#endif // PSR   
    
#ifdef SUBSURFACE_SCATTERING
    bool isSssPath = false;
#endif
    
    float3 primaryEmissive = sourceSurface.Emissive * psrPathThroughput + primaryEffectEmissive;
    float3 direct = primaryEmissive;

 #if defined(SHARC) && SHARC_DEBUG
    HashGridParameters gridParameters = GetSharcGridParameters();

    Output[idx] = float4(HashGridDebugColoredHash(sourceSurface.Position, sourceSurface.GeomNormal, gridParameters), 1);
    return;
#endif     
    
    // Handle direct lighting, with special treatment for delta lobes.
    // For non-delta lobes: standard NEE (EvaluateDirectRadiance) evaluates BSDF at sampled light directions.
    // For delta lobes: EvalDeltaLobeLighting checks if delta reflection/refraction directions fall within
    // each light source's solid angle, providing correct mirror reflections of analytical lights.
#if defined(NRD)
    float3 directDiffuse = 0.0f;
    float3 directSpecular = 0.0f;

    {
        const uint sourceLobes = sourceBSDF.GetLobes(sourceSurface);
        const bool sourceHasNonDeltaLobes = (sourceLobes & (uint)LobeType::NonDelta) != 0;
        const bool sourceHasDeltaLobes = (sourceLobes & (uint)LobeType::Delta) != 0;
        
        if (sourceHasNonDeltaLobes)
        {
#if defined(SUBSURFACE_SCATTERING)
            if (sourceSurface.SubsurfaceData.HasSubsurface != 0) {
                directDiffuse += EvaluateSubsurfaceDiffuseNEE(sourceSurface, sourceInstance, sourcePayload, sourceRayCone, randomSeed, true);
                isSssPath = true;
                // Specular uses the standard path with diffuse suppressed
                Surface specSurface = sourceSurface;
                specSurface.DiffuseAlbedo = 0;
                StandardBSDF specBsdf = StandardBSDF::make(specSurface, sourceSurface.Normal, sourceBRDFContext.ViewDirection, true);
                float3 dummyDiff, specDirect;
                EvaluateDirectRadiance(sourceMaterial.Type, sourceMaterial.Feature, specSurface, sourceBRDFContext, sourceInstance, specBsdf, randomSeed, true, dummyDiff, specDirect);
                directSpecular += specDirect;
            }
            else
#endif
                EvaluateDirectRadiance(sourceMaterial.Type, sourceMaterial.Feature, sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed, true, directDiffuse, directSpecular);
        }
        
        // Delta lobe lighting: check if delta reflection/refraction directions see any analytical lights.
        // Skip for pure delta surfaces — their delta lighting was captured in BUILD's stable radiance.
        if (sourceHasDeltaLobes && sourceHasNonDeltaLobes)
        {
            directSpecular += EvalDeltaLobeLighting(sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed, true);
        }
    }
#else
    {
        const uint sourceLobes = sourceBSDF.GetLobes(sourceSurface);
        const bool sourceHasNonDeltaLobes = (sourceLobes & (uint)LobeType::NonDelta) != 0;
        const bool sourceHasDeltaLobes = (sourceLobes & (uint)LobeType::Delta) != 0;
        
        if (sourceHasNonDeltaLobes)
        {
#if defined(SUBSURFACE_SCATTERING)
            if (sourceSurface.SubsurfaceData.HasSubsurface != 0) {
                    direct += EvaluateSubsurfaceDiffuseNEE(sourceSurface, sourceInstance, sourcePayload, sourceRayCone, randomSeed, true) * psrPathThroughput;
                isSssPath = true;
                // Specular uses the standard path with diffuse suppressed
                Surface specSurface = sourceSurface;
                specSurface.DiffuseAlbedo = 0;
                StandardBSDF specBsdf = StandardBSDF::make(specSurface, sourceSurface.Normal, sourceBRDFContext.ViewDirection, true);
                direct += EvaluateDirectRadiance(sourceMaterial.Type, sourceMaterial.Feature, specSurface, sourceBRDFContext, sourceInstance, specBsdf, randomSeed, true) * psrPathThroughput;
            }
            else
#endif
                direct += EvaluateDirectRadiance(sourceMaterial.Type, sourceMaterial.Feature, sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed, true) * psrPathThroughput;
        }
        
        // Delta lobe lighting: check if delta reflection/refraction directions see any analytical lights.
        // Skip for pure delta surfaces — their delta lighting was captured in BUILD's stable radiance.
        if (sourceHasDeltaLobes && sourceHasNonDeltaLobes)
        {
            direct += EvalDeltaLobeLighting(sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed, true) * psrPathThroughput;
        }
    }
#endif
    
    float3 direction;
#if defined(RAW_RADIANCE)
    MonteCarlo::BRDFWeight brdfWeight;
#endif

#if defined(NRD)
    float3 diffuseRadiance = directDiffuse * psrPathThroughput;
    float3 specularRadiance = directSpecular * psrPathThroughput;
#else
    float3 radiance = float3(0.0f, 0.0f, 0.0f);
#endif
    bool isSpecular = false;
    
#if defined(NRD)
     float diffHitDist = 0;     
     uint diffPathNum = 0;
     float specHitDist = NRD_FrontEnd_SpecHitDistAveraging_Begin();   
#elif defined(DLSS_RR)
    float specHitDist = 0.0f;
#endif

    RayDesc ray;
    Payload payload;

    Instance instance;
    LightingMaterialData material;

    Surface surface;
    BRDFContext brdfContext;

    StandardBSDF bsdf;
    
    RayCone rayCone;    
    
#if defined(SHARC)
    SharcState sharcState;
    SharcHitData sharcHitData;
#endif    
    
    [loop]
    for (uint i = 0; i < MAX_SAMPLES; i++)
    {
#if defined(SHARC) && SHARC_UPDATE
        SharcInit(sharcState);
#endif
        
        surface = sourceSurface;
        brdfContext = sourceBRDFContext;
        bsdf = sourceBSDF;
        rayCone = sourceRayCone; 
        
#if defined(NRD)
        float accumulatedHitDist = 0;
#endif        
        
        material = sourceMaterial;
        instance = sourceInstance;
        payload = sourcePayload;
        
        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = psrPathThroughput;
        bool arrivedViaDelta = false;
        float materialRoughnessPrev = 0.0f;
        bool isEnter = sourceIsEnter;
        bool isSpecularSample = false;

        // Water volume tracking for Beer-Lambert absorption
        bool insideWaterVolume = pathInsideWaterVolume;
        float3 waterVolumeAbsorption = pathWaterVolumeAbsorption;
        
#if defined(RAW_RADIANCE)
        float3 throughputDelta = float3(1.0f, 1.0f, 1.0f);
#endif        
        
        [loop]
        for (uint j = 0; j < MAX_BOUNCES; j++)
        {
            BSDFSample bsdfSample;
            bool isPrimaryReplacement = false;
            
            float3 faceNormalOriented = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0f ? surface.FaceNormal : -surface.FaceNormal;

#if LIGHTING_MODE == LIGHTING_MODE_DIFFUSE
            direction = surface.Mul(SampleCosineHemisphere(randomSeed));

            throughput *= surface.AO;
            throughput *= surface.Albedo;
            
            const bool hasTransmission = false;
#else            
            bool isValid = bsdf.SampleBSDF(brdfContext, material.Feature, surface, bsdfSample, randomSeed);
            
            if (isValid)
                direction = bsdfSample.wo;
            else
                break;
            
            bool isDelta = bsdfSample.isLobe(LobeType::Delta);
            isSpecular = bsdfSample.isLobe(LobeType::Specular) || isDelta;
            
            if (j == 0)
                isSpecularSample = isSpecular;         
                  
            bool hasTransmission = bsdfSample.isLobe(LobeType::Transmission);
            isPrimaryReplacement = surface.Primary && isDelta;
            arrivedViaDelta = isDelta;

            throughput *= bsdfSample.isLobe(LobeType::Transmission) ? 1.f : surface.AO;

            // Track water volume entry/exit on transmission
            if (hasTransmission && any(surface.VolumeAbsorption > 0.0f))
            {
                // isEnter (front face) + transmission = entering volume
                insideWaterVolume = isEnter;
                waterVolumeAbsorption = insideWaterVolume ? surface.VolumeAbsorption : float3(0.0f, 0.0f, 0.0f);
            }

#   if defined(RAW_RADIANCE)
            brdfWeight.diffuse = bsdfSample.isLobe(LobeType::DiffuseReflection) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
            brdfWeight.diffuse /= max(surface.DiffuseAlbedo, 1e-4f);
            brdfWeight.specular = (bsdfSample.isLobe(LobeType::SpecularReflection) || bsdfSample.isLobe(LobeType::DeltaReflection)) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
            brdfWeight.transmission = bsdfSample.isLobe(LobeType::Transmission) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);

            float3 brdfWeightOriginal = brdfWeight.diffuse * surface.DiffuseAlbedo + brdfWeight.specular + brdfWeight.transmission;

#       if defined(SHARC) && SHARC_UPDATE
            throughput *= brdfWeightOriginal;
#       else
            if (j > 0) {
                throughput *= brdfWeightOriginal;
            } else {
                float3 brdfWeightRaw = bsdfSample.weight;

                throughputDelta = brdfWeightOriginal / brdfWeightRaw;

                throughput *= brdfWeightRaw;
            }
#       endif
#   else    // RAW_RADIANCE
            throughput *= bsdfSample.weight;
#   endif   // !RAW_RADIANCE
#endif  
            
#if defined(SHARC) && SHARC_UPDATE
            SharcSetThroughput(sharcState, throughput);
#else

#   if RUSSIAN_ROULETTE != 0          
#       if defined(RAW_RADIANCE)
        // Apply russian roulette based on the original throughput
        float3 throughputColor = throughput * throughputDelta;
#       else
        float3 throughputColor = throughput;
#       endif            
#   endif
            
#   if RUSSIAN_ROULETTE == 1
            const float rrVal = 1.0f - min(1.0f, Color::RGBToLuminance(throughputColor));              
            const float rrProb = min(rrVal, 0.95f);

            if (Random(randomSeed) < rrProb)
                break;

            throughput /= (1.0f - rrProb); 
#   elif RUSSIAN_ROULETTE == 2
            const float rrVal = sqrt(Color::RGBToLuminance(throughputColor));            
            float rrProb = saturate(0.85 - rrVal);
            rrProb *= rrProb;

            rrProb = saturate(rrProb + max(0, ((float)j / (float)MAX_BOUNCES - 0.4f)));

            if (Random(randomSeed) < rrProb)
                break;

            throughput /= (1.0f - rrProb);
#   endif
#endif
            
#if defined(SHARC)
            materialRoughnessPrev += bsdfSample.isLobe(LobeType::Diffuse) ? 1.0f : surface.Roughness;
#endif
            
#if USE_SIA_INTERPOLATION
            ray.Origin = OffsetRaySIA(surface.Position, faceNormalOriented, surface.SIAOffset, hasTransmission);
#else
            ray.Origin = OffsetRay(surface.Position, faceNormalOriented, surface.PositionError, hasTransmission);
#endif
            ray.Direction = direction;
            ray.TMin = 0.0f;  // Offset already handles precision, no additional offset needed
            ray.TMax = RAY_TMAX;

            if (!bsdfSample.isLobe(LobeType::Delta))
                rayCone = RayCone::make(rayCone.getWidth(), min(rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF(bsdfSample.pdf), 2.0 * K_PI));

            payload = TraceRayStandard(Scene, ray, randomSeed);
            
            rayCone = rayCone.propagateDistance(payload.hitDistance);

            // Apply Beer-Lambert volume absorption for water
            if (insideWaterVolume)
            {
                throughput *= exp(-waterVolumeAbsorption * payload.hitDistance);
            }
            
#if defined(NRD)
            if (j == 0)
                accumulatedHitDist = payload.hitDistance;
#elif defined(DLSS_RR)
            if (isSpecularSample)
                specHitDist = payload.hitDistance;
#endif               
            
            if (!payload.Hit())
            {
                float3 skyIrradiance = SampleSky(SkyHemisphere, direction) * Raytracing.Sky;

#if defined(SHARC) && SHARC_UPDATE
                SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
#else
                sampleRadiance += skyIrradiance * throughput;
#endif                
                break;
            }
            
            float3 localPosition = ray.Origin + direction * payload.hitDistance;

            surface = SurfaceMaker::make(localPosition, payload, direction, rayCone, instance, material, isPrimaryReplacement);

#if defined(EFFECT_PASSTHROUGH)         
            // Pass through Effect materials in bounce: accumulate emissive, continue ray unchanged
            bool effectMiss = false;
            [loop]
            for (uint effectBouncePass = 0; effectBouncePass < 16 && material.Type == Type::Effect; effectBouncePass++)
            {
                sampleRadiance += surface.Emissive * throughput;
                float3 fn = dot(direction, surface.FaceNormal) <= 0.0f ? surface.FaceNormal : -surface.FaceNormal;
#if USE_SIA_INTERPOLATION
                ray.Origin = OffsetRaySIA(surface.Position, fn, surface.SIAOffset, true);
#else
                ray.Origin = OffsetRay(surface.Position, fn, surface.PositionError, true);
#endif
                ray.Direction = direction;
                ray.TMin = 0.0f;
                ray.TMax = RAY_TMAX;

                payload = TraceRayStandard(Scene, ray, randomSeed);
                rayCone = rayCone.propagateDistance(payload.hitDistance);

                if (insideWaterVolume)
                    throughput *= exp(-waterVolumeAbsorption * payload.hitDistance);

                if (!payload.Hit())
                {
                    effectMiss = true;
                    break;
                }

                localPosition = ray.Origin + direction * payload.hitDistance;
                surface = SurfaceMaker::make(localPosition, payload, direction, rayCone, instance, material, isPrimaryReplacement);
            }

            if (effectMiss)
            {
                float3 skyIrradiance = SampleSky(SkyHemisphere, direction) * Raytracing.Sky;
#if defined(SHARC) && SHARC_UPDATE
                SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
#else
                sampleRadiance += skyIrradiance * throughput;
#endif
                break;
            }
#endif

#if defined(SHARC)
            sharcHitData.positionWorld = surface.Position;
            sharcHitData.normalWorld = surface.GeomNormal;

#   if SHARC_ENABLE_SH_ENCODING
            sharcHitData.radianceDirectionWorld = -direction;
            sharcHitData.radianceDirectionWeight = saturate(1.0f - materialRoughnessPrev);
#   endif // SHARC_ENABLE_SH_ENCODING

#   if SHARC_SEPARATE_EMISSIVE
            sharcHitData.emissive = surface.Emissive;
#   endif // SHARC_SEPARATE_EMISSIVE

#   if !SHARC_UPDATE
            uint gridLevel = HashGridGetLevel(surface.Position, sharcParameters.hashGridParameters);
            float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.hashGridParameters);
            bool isValidHit = payload.hitDistance > voxelSize * sqrt(3.0f);
            
            if (isValidHit) {
                materialRoughnessPrev = min(materialRoughnessPrev, 0.99f);
                float a2 = materialRoughnessPrev * materialRoughnessPrev * materialRoughnessPrev * materialRoughnessPrev;
                float footprint = payload.hitDistance * sqrt(0.5f * a2 / max(1.0f - a2, DIV_EPSILON));
                isValidHit &= footprint > voxelSize * M_TO_GAME_UNIT;
                isValidHit &= material.Feature != Feature::kHairTint;
            }

            float3 sharcRadiance;
            if (!arrivedViaDelta && isValidHit && SharcGetCachedRadiance(sharcParameters, sharcHitData, sharcRadiance, false))
            {
                sampleRadiance += sharcRadiance * throughput;
                break;
            }
#   endif // !SHARC_UPDATE
#endif // SHARC  
            
            brdfContext = BRDFContext::make(surface, -direction);
            isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
            if (!isEnter) {
                surface.FlipNormal();
                brdfContext.NdotV = saturate(dot(surface.Normal, brdfContext.ViewDirection));
            }

            AdjustShadingNormal(surface, brdfContext, true, false);  // Adjusts the normal of the supplied shading frame to reduce black pixels due to back-facing view direction.
            bsdf = StandardBSDF::make(surface, surface.Normal, brdfContext.ViewDirection, isEnter);

            // Direct lighting with delta lobe support
            float3 directRadiance = 0.0f;
            const uint bounceLobes = bsdf.GetLobes(surface);
            const bool bounceHasNonDeltaLobes = (bounceLobes & (uint)LobeType::NonDelta) != 0;
            const bool bounceHasDeltaLobes = (bounceLobes & (uint)LobeType::Delta) != 0;
            
            if (bounceHasNonDeltaLobes)
            {
#ifdef SUBSURFACE_SCATTERING
                if (surface.SubsurfaceData.HasSubsurface != 0 && !isSssPath) {
                    directRadiance += EvaluateSubsurfaceDiffuseNEE(surface, instance, payload, rayCone, randomSeed, surface.Primary);
                    isSssPath = true;
                    // Specular uses the standard path with diffuse suppressed
                    Surface specSurface = surface;
                    specSurface.DiffuseAlbedo = 0;
                    StandardBSDF specBsdf = StandardBSDF::make(specSurface, surface.Normal, brdfContext.ViewDirection, isEnter);
                    directRadiance += EvaluateDirectRadiance(material.Type, material.Feature, specSurface, brdfContext, instance, specBsdf, randomSeed, surface.Primary);
                }
                else
#endif
                { 
                    directRadiance += EvaluateDirectRadiance(material.Type, material.Feature, surface, brdfContext, instance, bsdf, randomSeed, surface.Primary);
                }
            }
            
            // Delta lobe lighting: check if delta reflection/refraction directions see any analytical lights
            if (bounceHasDeltaLobes)
            {
                directRadiance += EvalDeltaLobeLighting(surface, brdfContext, instance, bsdf, randomSeed, surface.Primary);
            }
            
#if defined(SHARC) && SHARC_UPDATE
            sampleRadiance += directRadiance * throughput;
            if (!SharcUpdateHit(sharcParameters, sharcState, sharcHitData, directRadiance, Random(randomSeed)))
                return;

            throughput = float3(1.0f, 1.0f, 1.0f);
#else
            sampleRadiance += directRadiance * throughput;
            sampleRadiance += surface.Emissive * throughput;
#endif

        }

#if defined(NRD)
#   if defined(NRD_REBLUR)
        float normHitDist = REBLUR_FrontEnd_GetNormHitDist(accumulatedHitDist, depthVS, Raytracing.HitDistSettings.xyz, isSpecularSample ? sourceSurface.Roughness : 1.0);
#   else
        float normHitDist = accumulatedHitDist;
#   endif
        
        if (isSpecularSample) {
            NRD_FrontEnd_SpecHitDistAveraging_Add(specHitDist, normHitDist);        
        } else {
            diffHitDist += normHitDist;
            diffPathNum++;
        }
#endif        
        
#if defined(NRD)
        if (isSpecularSample)
            specularRadiance += sampleRadiance;
        else
            diffuseRadiance += sampleRadiance;
#else
        radiance += sampleRadiance;
#endif

#if defined(SHARC) && SHARC_UPDATE
        return;
#endif
    }

#if defined(NRD)
    NRD_FrontEnd_SpecHitDistAveraging_End(specHitDist);
    diffHitDist *= diffPathNum > 0 ? 1.0f / float(diffPathNum) : 0.0f;
    diffuseRadiance /= MAX_SAMPLES;
    specularRadiance /= MAX_SAMPLES;
#else
    radiance /= MAX_SAMPLES;
#endif        

#if !(defined(SHARC) && SHARC_UPDATE)
    // REFERENCE mode output
    // Apply primary ray water absorption when camera is underwater
    if (Camera.IsUnderwater != 0 && any(Camera.UnderwaterAbsorption > 0.0f))
    {
        float3 primaryWaterAttenuation = exp(-Camera.UnderwaterAbsorption * sourcePayload.hitDistance);
        primaryEmissive *= primaryWaterAttenuation;
        direct *= primaryWaterAttenuation;
#   if defined(NRD)
        diffuseRadiance *= primaryWaterAttenuation;
        specularRadiance *= primaryWaterAttenuation;
#   else
        radiance *= primaryWaterAttenuation;
#   endif
    }
    
#if defined(NRD)
    float3 diffFactor, specFactor;
    NRD_MaterialFactors(sourceSurface.Normal, sourceBRDFContext.ViewDirection, sourceSurface.DiffuseAlbedo, sourceSurface.F0, sourceSurface.Roughness, diffFactor, specFactor);    

    diffuseRadiance /= diffFactor;
    specularRadiance /= specFactor;    
    
    Output[idx] = float4(primaryEmissive, 1.0f);
#   if defined(NRD_REBLUR)
    DiffuseRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(diffuseRadiance, diffHitDist, true);
    SpecularRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(specularRadiance, specHitDist, true);  
#   else
    DiffuseRadiance[idx] = RELAX_FrontEnd_PackRadianceAndHitDist(diffuseRadiance, diffHitDist, true);
    SpecularRadiance[idx] = RELAX_FrontEnd_PackRadianceAndHitDist(specularRadiance, specHitDist, true);  
#   endif  
    
    DiffuseFactor[idx] = diffFactor;
    SpecularFactor[idx] = specFactor;    
#else    
    Output[idx] = float4(LLTrueLinearToGamma(direct + radiance), 1.0f);

#   if defined(DLSS_RR)
    SpecularHitDistance[idx] = specHitDist;     
#   endif    
#endif
        
#endif    
}
