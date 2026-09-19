#ifndef SER_HLSLI
#define SER_HLSLI

#if defined(ENABLE_SER) && ENABLE_SER && (!defined(USE_RAY_QUERY) || !USE_RAY_QUERY)
#   define SER_ENABLED 1
#else
#   define SER_ENABLED 0
#endif

// Octahedron map projection to an 8-bit key for ray direction coherence.
// Pack four bits per axis so all eight hint bits contribute to the key.
inline uint SER_CalculateRayCoherenceHint(float3 dir)
{
    float l1 = abs(dir.x) + abs(dir.y) + abs(dir.z);
    float3 p = (l1 > 1e-6f) ? (dir / l1) : float3(0.0f, 0.0f, 1.0f);
    float2 s = select(p.xy >= 0.0f, float2(1.0f, 1.0f), float2(-1.0f, -1.0f));
    float2 oct = (p.z >= 0.0f) ? p.xy : ((1.0f - abs(p.yx)) * s);
    uint2 grid = clamp((uint2)round((oct * 0.5f + 0.5f) * 15.0f), 0u, 15u);
    return (grid.y << 4) | grid.x;
}

inline uint SER_CalculateHitCoherenceHint(bool isHit, uint instanceID, uint materialType = 0)
{
    if (!isHit)
        return 0;
    return (1u << 15) | ((materialType & 0x7Fu) << 8) | (instanceID & 0xFFu);
}

#if SER_ENABLED
#   if defined(__spirv__)
// ---------------------------------------------------------------------------
// Vulkan SER via DXC SPIR-V intrinsics (SPV_NV_shader_invocation_reorder).
//
// DXC has no native HLSL support for shader invocation reordering, but it can
// emit the raw SPIR-V instructions through [[vk::ext_instruction]]. The hit
// object payload must be a module-scope variable in the RayPayloadKHR storage
// class, so the local payload is staged through g_SERPayload.
// ---------------------------------------------------------------------------
#       include "raytracing/include/Payload.hlsli"

#       define SER_SPV_SHADER_INVOCATION_REORDER_NV 5383
#       define SER_SPV_HIT_OBJECT_ATTRIBUTE_NV      5385
#       define SER_SPV_OP_HIT_OBJECT_TRACE_RAY_NV   5260
#       define SER_SPV_OP_HIT_OBJECT_EXECUTE_SHADER_NV 5264
#       define SER_SPV_OP_REORDER_THREAD_WITH_HIT_OBJECT_NV 5279
#       define SER_SPV_OP_REORDER_THREAD_WITH_HINT_NV 5280
#       define SER_SPV_OP_TYPE_HIT_OBJECT_NV        5281
#       define SER_SPV_RAY_PAYLOAD_KHR              5338

[[vk::ext_capability(SER_SPV_SHADER_INVOCATION_REORDER_NV)]]
[[vk::ext_extension("SPV_NV_shader_invocation_reorder")]]
[[vk::ext_type_def(SER_SPV_HIT_OBJECT_ATTRIBUTE_NV, SER_SPV_OP_TYPE_HIT_OBJECT_NV)]]
void SER_CreateHitObjectNV();
#       define HitObjectNV vk::ext_type<SER_SPV_HIT_OBJECT_ATTRIBUTE_NV>

[[vk::ext_storage_class(SER_SPV_RAY_PAYLOAD_KHR)]] static Payload g_SERPayload;

[[vk::ext_instruction(SER_SPV_OP_HIT_OBJECT_TRACE_RAY_NV)]]
void SER_HitObjectTraceRayNV(
    [[vk::ext_reference]] HitObjectNV hitObject,
    RaytracingAccelerationStructure as,
    uint rayFlags,
    uint cullMask,
    uint sbtOffset,
    uint sbtStride,
    uint missIndex,
    float3 rayOrigin,
    float rayTMin,
    float3 rayDirection,
    float rayTMax,
    [[vk::ext_reference]] [[vk::ext_storage_class(SER_SPV_RAY_PAYLOAD_KHR)]] Payload payload);

[[vk::ext_instruction(SER_SPV_OP_HIT_OBJECT_EXECUTE_SHADER_NV)]]
void SER_HitObjectExecuteShaderNV(
    [[vk::ext_reference]] HitObjectNV hitObject,
    [[vk::ext_reference]] [[vk::ext_storage_class(SER_SPV_RAY_PAYLOAD_KHR)]] Payload payload);

[[vk::ext_instruction(SER_SPV_OP_REORDER_THREAD_WITH_HIT_OBJECT_NV)]]
void SER_ReorderThreadWithHitObjectNV([[vk::ext_reference]] HitObjectNV hitObject);

[[vk::ext_instruction(SER_SPV_OP_REORDER_THREAD_WITH_HINT_NV)]]
void SER_ReorderThreadWithHintNV(int hint, int numBits);

// The hit object is an opaque type that must not be copied between
// instructions, so a single module-scope instance is shared by trace,
// reorder and invoke. Copying it (e.g. returning it by value) makes DXC emit
// distinct variables and the reorder/execute read uninitialized objects.
static HitObjectNV g_SERHitObject;

struct NvHitObject
{
    uint _unused;
};

inline NvHitObject NvTraceRayHitObject(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout Payload payload)
{
    SER_CreateHitObjectNV();

    g_SERPayload = payload;

    SER_HitObjectTraceRayNV(
        g_SERHitObject,
        scene,
        rayFlags,
        instanceInclusionMask,
        rayContributionToHitGroupIndex,
        multiplierForGeometryContributionToHitGroupIndex,
        missShaderIndex,
        ray.Origin,
        ray.TMin,
        ray.Direction,
        ray.TMax,
        g_SERPayload);

    NvHitObject hitObj;
    return hitObj;
}

inline void NvInvokeHitObject(
    RaytracingAccelerationStructure scene,
    NvHitObject hitObj,
    inout Payload payload)
{
    (void)scene; // Unused: the SPIR-V hit object carries its own scene reference.
    (void)hitObj;
    SER_HitObjectExecuteShaderNV(g_SERHitObject, g_SERPayload);
    payload = g_SERPayload;
}

inline void SER_ReorderHitObject(NvHitObject hitObj, uint coherenceHint = 0, uint numBits = 0)
{
    (void)hitObj;
    (void)coherenceHint;
    (void)numBits;
    SER_ReorderThreadWithHitObjectNV(g_SERHitObject);
}

inline void SER_ReorderThread(uint coherenceHint, uint numBits = 16)
{
    SER_ReorderThreadWithHintNV(int(coherenceHint), int(numBits));
}
#   else // !__spirv__
// ---------------------------------------------------------------------------
// D3D12 SER via the NVAPI shader extension slot.
// ---------------------------------------------------------------------------
#       ifndef NV_HLSL_EXTNS_INCLUDED
#           define NV_HLSL_EXTNS_INCLUDED 1
#           ifndef NV_SHADER_EXTN_SLOT
#               define NV_SHADER_EXTN_SLOT u127
#               define NV_SHADER_EXTN_REGISTER_SPACE space0
#           endif
#           include "include/nvapi/nvHLSLExtns.h"
#       endif

inline void SER_ReorderThread(uint coherenceHint, uint numBits = 16)
{
    NvReorderThread(coherenceHint, numBits);
}

inline void SER_ReorderHitObject(NvHitObject hitObj, uint coherenceHint = 0, uint numBits = 0)
{
    if (numBits > 0)
        NvReorderThread(hitObj, coherenceHint, numBits);
    else
        NvReorderThread(hitObj);
}
#   endif
#endif // SER_ENABLED

#if !SER_ENABLED && !defined(__spirv__) && defined(DEBUG_TRACE_HEATMAP) && DEBUG_TRACE_HEATMAP
#   ifndef NV_HLSL_EXTNS_INCLUDED
#       define NV_HLSL_EXTNS_INCLUDED 1
#       ifndef NV_SHADER_EXTN_SLOT
#           define NV_SHADER_EXTN_SLOT u127
#           define NV_SHADER_EXTN_REGISTER_SPACE space0
#       endif
#       include "include/nvapi/nvHLSLExtns.h"
#   endif
#endif

#endif // SER_HLSLI
