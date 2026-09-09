#include "interop/Light.hlsli"
#include "interop/Instance.hlsli"

// Builds the compacted per-instance light index list on the GPU.
//
// One thread per instance: counts the lights whose influence volume overlaps the instance's world
// bound, atomically reserves a contiguous range in the global list, then writes the matching light
// indices. The ranges are not ordered by instance, but each range is contiguous, which keeps the
// shader-side random access sampling (uniform / RIS) intact.

struct PushConstants
{
    uint NumInstances;
    uint NumLights;
    uint ListCapacity;
    uint Pad;
};

ConstantBuffer<PushConstants> PC : register(b0);

StructuredBuffer<Light>       Lights           : register(t0);
StructuredBuffer<float4>      InstanceBounds   : register(t1);

RWStructuredBuffer<Instance>  Instances        : register(u0);
RWStructuredBuffer<uint>      InstanceLightList : register(u1);
RWStructuredBuffer<uint>      LightCounter     : register(u2);

// Mirrors BLASCluster::UpdateInstanceLightData's conservative tests.
bool LightAffectsInstance(Light light, float4 bound)
{
    if ((light.Flags & LightFlags::Active) == 0)
        return false;

    if (light.Type == LightType::Directional)
        return true;

    const float3 center = bound.xyz;
    const float boundRadius = bound.w;

    const float3 toCenter = center - light.Position;
    const float dist = length(toCenter);

    // Sphere vs light radius
    if (dist - boundRadius > light.Radius)
        return false;

    // Sphere vs spot cone (conservative): the bound must reach the cone frustum.
    // CosOuterAngle is the half-angle cosine, matching the shader's smoothstep.
    if (light.Type == LightType::Spot)
    {
        const float distAlong = dot(toCenter, light.Direction);

        // Entirely behind the light's apex plane
        if (distAlong < -boundRadius)
            return false;

        // cosOuter <= 0 means the cone is a hemisphere or wider - nothing to cull
        const float cosOuter = clamp(light.CosOuterAngle, -1.0f, 1.0f);
        if (cosOuter > 0.0f)
        {
            const float perpDist = length(toCenter - light.Direction * distAlong);
            const float coneRadius = max(distAlong, 0.0f) * tan(acos(cosOuter));
            if (perpDist - boundRadius > coneRadius)
                return false;
        }
    }

    return true;
}

[numthreads(64, 1, 1)]
void Main(uint3 DTid : SV_DispatchThreadID)
{
    const uint instanceIndex = DTid.x;
    if (instanceIndex >= PC.NumInstances)
        return;

    Instance instance = Instances[instanceIndex];
    const float4 bound = InstanceBounds[instanceIndex];

    uint count = 0;
    for (uint lightIdx = 0; lightIdx < PC.NumLights; ++lightIdx)
    {
        if (LightAffectsInstance(Lights[lightIdx], bound))
            count++;
    }

    if (count == 0)
    {
        instance.LightData.LightOffset = 0;
        instance.LightData.LightCount = 0;
        Instances[instanceIndex] = instance;
        return;
    }

    // Reserve a contiguous range. The atomic reservation guarantees a unique base per instance,
    // so no two instances ever write the same list entries.
    uint base = 0;
    InterlockedAdd(LightCounter[0], count, base);

    if (base >= PC.ListCapacity)
    {
        instance.LightData.LightOffset = 0;
        instance.LightData.LightCount = 0;
        Instances[instanceIndex] = instance;
        return;
    }

    const uint writable = min(count, PC.ListCapacity - base);

    uint cursor = base;
    const uint cursorEnd = base + writable;
    for (uint lightIdx = 0; lightIdx < PC.NumLights && cursor < cursorEnd; ++lightIdx)
    {
        if (LightAffectsInstance(Lights[lightIdx], bound))
        {
            InstanceLightList[cursor] = lightIdx;
            cursor++;
        }
    }

    instance.LightData.LightOffset = (uint16_t)base;
    instance.LightData.LightCount = (uint16_t)(cursor - base);
    Instances[instanceIndex] = instance;
}
