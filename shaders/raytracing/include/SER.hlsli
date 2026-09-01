#ifndef SER_HLSLI
#define SER_HLSLI

#if defined(ENABLE_SER) && ENABLE_SER && (!defined(USE_RAY_QUERY) || !USE_RAY_QUERY)
#   define SER_ENABLED 1
#else
#   define SER_ENABLED 0
#endif

#if SER_ENABLED || (defined(DEBUG_TRACE_HEATMAP) && DEBUG_TRACE_HEATMAP)
#   ifndef NV_HLSL_EXTNS_INCLUDED
#       define NV_HLSL_EXTNS_INCLUDED 1
#       ifndef NV_SHADER_EXTN_SLOT
#           define NV_SHADER_EXTN_SLOT u127
#           define NV_SHADER_EXTN_REGISTER_SPACE space0
#       endif
#       include "include/nvapi/nvHLSLExtns.h"
#   endif
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

inline void SER_ReorderThread(uint coherenceHint, uint numBits = 16)
{
#if SER_ENABLED
    NvReorderThread(coherenceHint, numBits);
#endif
}

#if SER_ENABLED
inline void SER_ReorderHitObject(NvHitObject hitObj, uint coherenceHint = 0, uint numBits = 0)
{
    if (numBits > 0)
        NvReorderThread(hitObj, coherenceHint, numBits);
    else
        NvReorderThread(hitObj);
}
#endif

#endif // SER_HLSLI
