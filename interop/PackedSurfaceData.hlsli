#ifndef PACKED_SURFACE_DATA_HLSLI
#define PACKED_SURFACE_DATA_HLSLI

// Packed primary surface data for ReSTIR GI.
// Written by path tracer into a ping-pong StructuredBuffer,
// read by ReSTIR GI passes to reconstruct Surface + BRDFContext.
// 48 bytes per pixel (12 uints).

#ifdef __cplusplus
#include <cstdint>

struct PackedSurfaceData
{
    float    posW[3];           // 12
    uint32_t packedNormal;      // 4
    uint32_t packedTangent;     // 4
    uint32_t packedBitangent;   // 4
    uint32_t packedFaceNormal;  // 4
    uint32_t packedViewDir;     // 4
    uint32_t diffuseAlbedo;     // 4  (R11G11B10 unorm)
    uint32_t specularF0;        // 4  (R11G11B10 unorm)
    uint32_t roughMetallic;     // 4  (fp16 roughness | fp16 metallic)
    float    viewDepth;         // 4  (negative = empty)
};

static_assert(sizeof(PackedSurfaceData) == 48, "PackedSurfaceData must be 48 bytes");
static constexpr uint32_t PACKED_SURFACE_DATA_STRIDE = 48;

#else // HLSL

struct PackedSurfaceData
{
    float3 posW;
    uint   packedNormal;
    uint   packedTangent;
    uint   packedBitangent;
    uint   packedFaceNormal;
    uint   packedViewDir;
    uint   diffuseAlbedo;
    uint   specularF0;
    uint   roughMetallic;
    float  viewDepth;           // negative = empty surface
};

// ---------------------------------------------------------------------------
// Self-contained packing utilities (no external dependencies)
// ---------------------------------------------------------------------------

// Standard octahedral encoding: unit direction -> uint32 (unorm16x2)
uint PSD_PackOct(float3 n)
{
    float2 p = n.xy * (1.0 / (abs(n.x) + abs(n.y) + abs(n.z)));
    if (n.z <= 0.0)
        p = (1.0 - abs(p.yx)) * select(p >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
    float2 u = p * 0.5 + 0.5;
    uint x = (uint)(saturate(u.x) * 65535.0 + 0.5);
    uint y = (uint)(saturate(u.y) * 65535.0 + 0.5);
    return x | (y << 16);
}

// uint32 (unorm16x2) -> unit direction
float3 PSD_UnpackOct(uint packed)
{
    float2 p = float2(
        (packed & 0xFFFF) / 65535.0,
        (packed >> 16) / 65535.0
    ) * 2.0 - 1.0;
    float3 n = float3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0)
    {
        float2 s = select(n.xy >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
        n.xy = s * (1.0 - abs(n.yx));
    }
    return normalize(n);
}

// [0,1] color -> R11G11B10 unorm packed uint
uint PSD_PackColor(float3 c)
{
    c = saturate(c);
    uint r = (uint)(c.r * 2047.0 + 0.5);
    uint g = (uint)(c.g * 2047.0 + 0.5);
    uint b = (uint)(c.b * 1023.0 + 0.5);
    return r | (g << 11) | (b << 22);
}

// R11G11B10 unorm -> [0,1] color
float3 PSD_UnpackColor(uint packed)
{
    return float3(
        (packed & 0x7FF) / 2047.0,
        ((packed >> 11) & 0x7FF) / 2047.0,
        ((packed >> 22) & 0x3FF) / 1023.0
    );
}

// ---------------------------------------------------------------------------
// Pack: Surface fields -> PackedSurfaceData
// ---------------------------------------------------------------------------
PackedSurfaceData PSD_Pack(
    float3 position, float3 normal, float3 tangent, float3 bitangent,
    float3 faceNormal, float3 viewDir,
    float3 diffAlbedo, float3 f0,
    float roughness, float metallic,
    float viewDepth)
{
    PackedSurfaceData d;
    d.posW             = position;
    d.packedNormal     = PSD_PackOct(normal);
    d.packedTangent    = PSD_PackOct(tangent);
    d.packedBitangent  = PSD_PackOct(bitangent);
    d.packedFaceNormal = PSD_PackOct(faceNormal);
    d.packedViewDir    = PSD_PackOct(viewDir);
    d.diffuseAlbedo    = PSD_PackColor(diffAlbedo);
    d.specularF0       = PSD_PackColor(f0);
    d.roughMetallic    = f32tof16(roughness) | (f32tof16(metallic) << 16);
    d.viewDepth        = viewDepth;
    return d;
}

// Empty (sky/invalid) surface marker
PackedSurfaceData PSD_Empty()
{
    PackedSurfaceData d = (PackedSurfaceData)0;
    d.viewDepth = -1.0;
    return d;
}

bool PSD_IsEmpty(PackedSurfaceData d)
{
    return d.viewDepth < 0.0;
}

#endif // __cplusplus
#endif // PACKED_SURFACE_DATA_HLSLI
