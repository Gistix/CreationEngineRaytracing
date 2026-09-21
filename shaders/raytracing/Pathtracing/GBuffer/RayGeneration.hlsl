#include "raytracing/Pathtracing/GBuffer/Registers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "raytracing/include/Payload.hlsli"

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

#include "raytracing/include/Rays.hlsli"

#if defined(GROUP_TILING)
#   define DXC_STATIC_DISPATCH_GRID_DIM 1
#   include "include/ThreadGroupTilingX.hlsli"
#endif

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

    RayDesc sourceRay = SetupPrimaryRay(idx, size, Camera);
    
    const float3 sourceDirection = sourceRay.Direction;
    
    uint randomSeed = InitRandomSeed(idx, size, Camera.FrameIndex);

    Payload sourcePayload = TraceRayStandard(Scene, sourceRay, randomSeed, true);
 
    [branch]
    if (!sourcePayload.Hit())
    {
        Depth[idx] = 1;
        
        Albedo[idx] = half4(0.0f, 0.0f, 0.0f, 0.0f);
        
        EmissiveMetallic[idx] = half4(0.0f, 0.0f, 0.0f, 0.0f);
        
        NormalRoughness[idx] = half4(0.0f, 0.0f, 0.0f, 1.0f);

        const float3 skyVirtualPos = sourceDirection * SKY_DISTANCE;
        const float3 skyVirtualPosPrev = skyVirtualPos + (Camera.Position - Camera.PositionPrev);
        MotionVectors[idx] = half4(computeMotionVectorCameraRelative(skyVirtualPos, skyVirtualPosPrev), 0);
   
        Material[idx] = 0;
    }
    else
    {
        RayCone sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * sourcePayload.hitDistance, Raytracing.PixelConeSpreadAngle);
    
        float3 sourcePosition = Camera.Position.xyz + sourceDirection * sourcePayload.hitDistance;
    
        Instance sourceInstance;
        LightingMaterialData sourceMaterial;

        Surface sourceSurface = SurfaceMaker::make(sourcePosition, sourcePayload, sourceDirection, sourceRayCone, sourceInstance, sourceMaterial, true);

        Depth[idx] = computeClipDepthCameraRelative(sourceSurface.CameraRelativePosition);
        
        Albedo[idx] = half4(sourceSurface.Albedo, 1.0f);
        
        EmissiveMetallic[idx] = half4(sourceSurface.Emissive, sourceSurface.Metallic);
        
        const bool useCoat = sourceSurface.CoatStrength > 0;
        NormalRoughness[idx] = half4(
            normalize(useCoat ? sourceSurface.CoatNormal : sourceSurface.Normal),
            saturate(useCoat ? sourceSurface.CoatRoughness : sourceSurface.Roughness)
        );

        MotionVectors[idx] = half4(computeMotionVectorCameraRelative(sourceSurface.CameraRelativePosition, sourceSurface.PrevCameraRelativePosition), 0);

        const uint type = uint(sourceMaterial.Type);
        const uint feature = uint(sourceMaterial.Feature);
        const uint unused1 = 0;
        const uint unused2 = 0;
        
        Material[idx] = (type & 0xFFu) | ((feature & 0xFFu) << 8) | ((unused1 & 0xFFu) << 16) | ((unused2 & 0xFFu) << 24);
    }
}
