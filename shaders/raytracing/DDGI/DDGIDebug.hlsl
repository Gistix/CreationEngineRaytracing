#include "interop/CameraData.hlsli"
#include "interop/DDGIData.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<DDGIData> DDGI : register(b1);

Texture2DArray<float4> ProbeIrradiance : register(t0);
Texture2DArray<float4> ProbeDistance : register(t1);
Texture2DArray<float4> ProbeData : register(t2);
Texture2DArray<float4> RayData : register(t3);
RWTexture2D<float4> Output : register(u0);

uint3 GetProbeCounts()
{
    const uint packedCounts = DDGI.Volume.packed0;
    return uint3(packedCounts & 0x3FF, (packedCounts >> 10) & 0x3FF, (packedCounts >> 20) & 0x3FF);
}

void GetSliceCoordinates(uint2 idx, uint2 outputSize, uint layerCount, out uint layer, out float2 localUV)
{
    const float aspect = (float)outputSize.x / max((float)outputSize.y, 1.0f);
    const uint columns = max(1u, (uint)ceil(sqrt((float)layerCount * aspect)));
    const uint rows = max(1u, (layerCount + columns - 1) / columns);
    const float2 gridUV = (float2(idx) + 0.5f) / outputSize * float2(columns, rows);
    const uint2 cell = (uint2)gridUV;

    layer = cell.y * columns + cell.x;
    localUV = frac(gridUV);
}

float3 ToneMapRadiance(float3 radiance)
{
    radiance = max(radiance, 0.0f);
    return pow(radiance / (1.0f + radiance), 1.0f / 2.2f);
}

float DistanceToGrayscale(float distance)
{
    const float maxDistance = max(DDGI.Volume.probeMaxRayDistance, 1.0f);
    const float value = log2(1.0f + min(abs(distance), maxDistance)) / log2(1.0f + maxDistance);
    return saturate(value);
}

[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    const uint2 outputSize = Camera.RenderSize;
    if (any(idx >= outputSize))
        return;

    const uint3 probeCounts = GetProbeCounts();
    const uint view = DDGI.DebugView;
    uint layer;
    float2 localUV;
    GetSliceCoordinates(idx, outputSize, probeCounts.z, layer, localUV);

    if (layer >= probeCounts.z)
    {
        const float checker = ((idx.x / 16 + idx.y / 16) & 1) ? 0.12f : 0.06f;
        Output[idx] = float4(checker.xxx, 1.0f);
        return;
    }

    float3 color = 0.0f;
    uint2 texel;

    if (view == 1) // Probe irradiance atlas
    {
        const uint2 size = 8 * probeCounts.xy;
        texel = min((uint2)(localUV * size), size - 1);
        color = ToneMapRadiance(ProbeIrradiance.Load(int4(texel, layer, 0)).rgb);
    }
    else if (view == 2) // Probe distance atlas
    {
        const uint2 size = 16 * probeCounts.xy;
        texel = min((uint2)(localUV * size), size - 1);
        const float2 distanceMoments = ProbeDistance.Load(int4(texel, layer, 0)).rg;
        const float meanDistance = DistanceToGrayscale(distanceMoments.x);
        const float variance = saturate(abs(distanceMoments.y - distanceMoments.x * distanceMoments.x) /
            max(distanceMoments.x * distanceMoments.x, 1.0f));
        color = float3(meanDistance, variance, 1.0f - meanDistance);
    }
    else if (view == 3) // Probe classification state
    {
        const uint2 size = probeCounts.xy;
        texel = min((uint2)(localUV * size), size - 1);
        const float state = ProbeData.Load(int4(texel, layer, 0)).w;
        color = state == 0.0f ? float3(0.1f, 0.9f, 0.2f) : float3(0.95f, 0.1f, 0.05f);
    }
    else if (view == 4) // Probe relocation offsets
    {
        const uint2 size = probeCounts.xy;
        texel = min((uint2)(localUV * size), size - 1);
        const float3 offset = ProbeData.Load(int4(texel, layer, 0)).xyz;
        color = saturate(0.5f + offset);
    }
    else if (view == 5) // Probe ray radiance
    {
        const uint2 size = uint2(DDGI.Volume.packed2 & 0xFFFF, probeCounts.x * probeCounts.y);
        texel = min((uint2)(localUV * size), size - 1);
        color = ToneMapRadiance(RayData.Load(int4(texel, layer, 0)).rgb);
    }
    else if (view == 6) // Probe ray hit distance; red marks a backface
    {
        const uint2 size = uint2(DDGI.Volume.packed2 & 0xFFFF, probeCounts.x * probeCounts.y);
        texel = min((uint2)(localUV * size), size - 1);
        const float distance = RayData.Load(int4(texel, layer, 0)).a;
        color = distance < 0.0f
            ? float3(DistanceToGrayscale(distance), 0.0f, 0.0f)
            : DistanceToGrayscale(distance).xxx;
    }

    const float border = any(localUV < 0.002f) || any(localUV > 0.998f) ? 0.0f : 1.0f;
    Output[idx] = float4(color * border, 1.0f);
}
