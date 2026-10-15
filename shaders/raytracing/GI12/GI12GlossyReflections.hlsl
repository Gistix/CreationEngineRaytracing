/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
MIT License
********************************************************************/

#include "GI12Registers.hlsli"
#include "GI12Common.hlsli"

// Glossy Reflections Buffers
RWStructuredBuffer<uint>  g_GlossyReflections_RtSampleBuffer          : register(u0);
RWStructuredBuffer<uint>  g_GlossyReflections_RtSampleCountBuffer     : register(u1);
RWTexture2D<float4>       g_GlossyReflections_SpecularBuffer          : register(u2);
RWTexture2D<float4>       g_GlossyReflections_DirectionBuffer         : register(u3);
RWTexture2D<float4>       g_GlossyReflections_ReflectionsBuffer0      : register(u4);
RWTexture2D<float4>       g_GlossyReflections_AverageSquaredBuffer0   : register(u5);
RWTexture2D<float4>       g_GlossyReflections_ReflectionsBuffer1      : register(u6);
RWTexture2D<float4>       g_GlossyReflections_AverageSquaredBuffer1   : register(u7);
RWTexture2D<float>        g_GlossyReflections_FirefliesBuffer         : register(u8);
RWTexture2D<float4>       g_GlossyReflections_StandardDevBuffer       : register(u9);
RWTexture2D<float4>       g_ReflectionBuffer                          : register(u10);

Texture2D<float4>         g_PreviousReflectionBuffer                  : register(t11);
Texture2D<float>          g_PreviousDepthBuffer                       : register(t12);
Texture2D<snorm float4>   g_PreviousNormalBuffer                      : register(t13);

int2 GlossyReflections_HalfRes()
{
    return GlossyReflectionsCB.half_res
        ? (GlossyReflectionsCB.full_res + 1) >> 1
        : GlossyReflectionsCB.full_res;
}

int2 GlossyReflections_FullRes()
{
    return GlossyReflectionsCB.full_res;
}

int2 GlossyReflections_HalfToFullRes(in int2 half_pos)
{
    return GlossyReflectionsCB.half_res
        ? (half_pos << int2(1, 1)) + int2((GI12CB.frame_index >> 0) & 1,
                                          (GI12CB.frame_index >> 1) & 1)
        : half_pos;
}

int2 GlossyReflections_FullToHalfRes(in int2 full_pos)
{
    return GlossyReflectionsCB.half_res ? (full_pos >> 1) : full_pos;
}

int GlossyReflections_FullToHalfRadius(in int full_radius)
{
    return GlossyReflectionsCB.half_res ? ((full_radius + 1) >> 1) : full_radius;
}

float3 ScreenToWorld(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(GI12CB.view_proj_inv, clip);
    return world.xyz / world.w + GI12CB.eye;
}

float3 SampleSky(float3 dir)
{
    float3 d = dir;
    d.z = max(d.z, 0.0f);
    float r = sqrt(max(1.0f - d.z, 0.0f));
    float phi = atan2(d.y, d.x);
    float2 uv = float2(cos(phi), sin(phi)) * r * 0.5f + 0.5f;
    return SkyHemisphere.SampleLevel(LinearClampSampler, uv, 0.0f).rgb * Raytracing.Sky;
}

float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

// -------------------------------------------------------------
// Clear Counters Kernel
// -------------------------------------------------------------
[numthreads(1, 1, 1)]
void ClearCounters()
{
    g_GlossyReflections_RtSampleCountBuffer[0] = 0;
}

// -------------------------------------------------------------
// Trace Reflections Kernel (RayQuery inline RT)
// -------------------------------------------------------------
[numthreads(64, 1, 1)]
void TraceReflections(uint did : SV_DispatchThreadID)
{
    if (did >= g_GlossyReflections_RtSampleCountBuffer[0])
        return;

    int2 full_pos = GlossyReflections_UnpackSample(g_GlossyReflections_RtSampleBuffer[did]);
    if (any(full_pos >= GlossyReflections_FullRes()))
        return;

    float depth = Depth.Load(int3(full_pos, 0));
    if (depth >= 1.0f)
        return;

    float4 normRough = NormalRoughness.Load(int3(full_pos, 0));
    float3 normal = normalize(normRough.xyz);
    float roughness = normRough.w;

    float2 uv = (float2(full_pos) + 0.5f) / float2(GlossyReflections_FullRes());
    float3 worldPos = ScreenToWorld(uv, depth);
    float3 viewDir = normalize(GI12CB.eye - worldPos);

    // GGX reflection direction
    uint seed = xxHash((uint)full_pos.x ^ ((uint)full_pos.y << 16) ^ GI12CB.frame_index);
    float rx = rand_pcg(seed);
    float ry = rand_pcg(seed);
    float2 randVal = float2(rx, ry);

    float3 tangent = normalize(cross(abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f), normal));
    float3 bitangent = cross(normal, tangent);
    float3 localView = float3(dot(viewDir, tangent), dot(viewDir, bitangent), dot(viewDir, normal));
    float alpha = max(roughness * roughness, 1e-3f);

    float3 v = normalize(float3(alpha * localView.x, alpha * localView.y, localView.z));
    float3 t1 = (v.z < 0.999f) ? normalize(cross(v, float3(0, 0, 1))) : float3(1, 0, 0);
    float3 t2 = cross(t1, v);
    float r = sqrt(randVal.x);
    float phi = 2.0f * PI * randVal.y;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    float s = 0.5f * (1.0f + v.z);
    p2 = (1.0f - s) * sqrt(max(0.0f, 1.0f - p1 * p1)) + s * p2;
    float3 h = p1 * t1 + p2 * t2 + sqrt(max(0.0f, 1.0f - p1 * p1 - p2 * p2)) * v;
    float3 localH = normalize(float3(alpha * h.x, alpha * h.y, max(0.0f, h.z)));
    float3 localDir = reflect(-localView, localH);
    float3 dir = normalize(localDir.x * tangent + localDir.y * bitangent + localDir.z * normal);
    if (dot(normal, dir) <= 0.0f)
        dir = reflect(-viewDir, normal);

    float farZ = max(GI12CB.near_far.y, 100000.0f);

    RayQuery<RAY_FLAG_NONE> rayQuery;
    RayDesc ray;
    ray.Origin = worldPos + normal * 0.5f;
    ray.Direction = dir;
    ray.TMin = 0.1f;
    ray.TMax = farZ;

    rayQuery.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, ray);
    while (rayQuery.Proceed())
    {
        rayQuery.CommitNonOpaqueTriangleHit();
    }

    float3 radiance = float3(0.0f, 0.0f, 0.0f);
    float hitDist = -1.0f;

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        hitDist = rayQuery.CommittedRayT();
        float3 hitPos = ray.Origin + dir * hitDist;

        // Directional Sun/Moon light with shadow ray
        float3 sunDir = normalize(Raytracing.DirectionalLight.Direction);
        float sunNdotL = max(0.0f, dot(-dir, sunDir));
        if (sunNdotL > 0.0f && (Raytracing.DirectionalLight.Flags & LightFlags::Active))
        {
            RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> shadowQuery;
            RayDesc shadowRay;
            shadowRay.Origin = hitPos + (-dir) * 0.5f;
            shadowRay.Direction = sunDir;
            shadowRay.TMin = 0.1f;
            shadowRay.TMax = 10000.0f;
            shadowQuery.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, shadowRay);
            shadowQuery.Proceed();
            if (shadowQuery.CommittedStatus() == COMMITTED_NOTHING)
            {
                radiance += Raytracing.DirectionalLight.Color.rgb * (sunNdotL * Raytracing.Directional * (1.0f / PI));
            }
        }

        // Local active lights
        uint numLights = min(Raytracing.NumLights, 16u);
        for (uint li = 0; li < numLights; ++li)
        {
            Light l = Lights[li];
            if (!(l.Flags & LightFlags::Active))
                continue;

            float3 toLight = l.Position.xyz - hitPos;
            float distSq = dot(toLight, toLight);
            if (distSq > l.Radius * l.Radius)
                continue;

            float dist = sqrt(distSq);
            float3 L = toLight / max(dist, 1e-4f);
            float atten = saturate(1.0f - dist * l.InvRadius);
            atten *= atten;

            float ndl = max(0.0f, dot(-dir, L));
            if (ndl > 0.0f)
            {
                radiance += l.Color.rgb * (atten * ndl * Raytracing.Point * (1.0f / PI));
            }
        }

        // Sky ambient bounce
        float3 sky = SampleSky(float3(0.0f, 0.0f, 1.0f));
        radiance += sky * 0.15f;
    }
    else
    {
        radiance = SampleSky(dir);
        hitDist = farZ;
    }

    // Tone map radiance to compress fireflies
    radiance /= (1.0f + radiance);

    int2 half_pos = GlossyReflections_FullToHalfRes(full_pos);
    if (GlossyReflectionsCB.half_res)
    {
        g_GlossyReflections_SpecularBuffer[half_pos] = float4(radiance, hitDist);
        g_GlossyReflections_DirectionBuffer[half_pos] = float4(dir, 0.0f);
    }
    else
    {
        g_GlossyReflections_SpecularBuffer[full_pos] = float4(radiance, hitDist);
        g_GlossyReflections_DirectionBuffer[full_pos] = float4(dir, 0.0f);
    }
}

// -------------------------------------------------------------
// Resolve Reflections Atrous First Pass
// -------------------------------------------------------------
[numthreads(8, 8, 1)]
void ResolveReflections_Atrous_First(uint2 did : SV_DispatchThreadID)
{
    int2 half_pos = int2(did);
    if (any(half_pos >= GlossyReflections_HalfRes()))
        return;

    int2 full_pos = GlossyReflections_HalfToFullRes(half_pos);
    if (any(full_pos >= GlossyReflections_FullRes()))
        return;

    float depth = Depth.Load(int3(full_pos, 0));
    if (depth >= 1.0f)
        return;

    float4 normRough = NormalRoughness.Load(int3(full_pos, 0));
    float3 normal = normalize(normRough.xyz);
    float roughness = normRough.w;

    if (roughness > GlossyReflectionsCB.high_roughness_threshold)
        return;

    float2 uv = (float2(full_pos) + 0.5f) / float2(GlossyReflections_FullRes());
    float3 centerWorld = ScreenToWorld(uv, depth);
    float3 centerNormal = normal;

    float4 reflection_0 = 0.0f;
    float3 reflection_avg_sqr_0 = 0.0f;
    float  reflection_weight_0 = 0.0f;

    int full_step = GlossyAtrousCB.full_step;
    int half_step = max(GlossyReflections_FullToHalfRadius(full_step), 1);
    int half_radius = max(GlossyReflections_FullToHalfRadius(2 * full_step), 1);

    for (int half_offset_x = -half_radius; half_offset_x <= half_radius; half_offset_x += half_step)
    {
        for (int half_offset_y = -half_radius; half_offset_y <= half_radius; half_offset_y += half_step)
        {
            int2 half_sample_pos = half_pos + int2(half_offset_x, half_offset_y);
            int2 full_sample_pos = GlossyReflections_HalfToFullRes(half_sample_pos);

            if (any(full_sample_pos < 0) || any(full_sample_pos >= GlossyReflections_FullRes()))
                continue;

            float sample_depth = Depth.Load(int3(full_sample_pos, 0));
            if (sample_depth >= 1.0f)
                continue;

            float sample_roughness = NormalRoughness.Load(int3(full_sample_pos, 0)).w;
            if (sample_roughness > GlossyReflectionsCB.high_roughness_threshold)
                continue;

            float2 sample_uv = (float2(full_sample_pos) + 0.5f) / float2(GlossyReflections_FullRes());
            float3 sample_world = ScreenToWorld(sample_uv, sample_depth);
            float plane_weight = saturate(1.0f - abs(dot(centerNormal, centerWorld - sample_world)) * 20.0f);

            int2 filter_pos = full_sample_pos - full_pos;
            float filter_weight = AtrousRatioEstimator_GaussianFilter((float)filter_pos.x, (float)full_step)
                                * AtrousRatioEstimator_GaussianFilter((float)filter_pos.y, (float)full_step);

            float4 radiance_hit = g_GlossyReflections_SpecularBuffer[half_sample_pos];
            float weight = filter_weight * plane_weight;

            reflection_0 += radiance_hit * weight;
            reflection_avg_sqr_0 += radiance_hit.xyz * radiance_hit.xyz * weight;
            reflection_weight_0 += weight;
        }
    }

    if (reflection_weight_0 > 1e-3f)
    {
        reflection_0 /= reflection_weight_0;
        reflection_avg_sqr_0 /= reflection_weight_0;
    }

    g_GlossyReflections_ReflectionsBuffer0[half_pos] = reflection_0;
    g_GlossyReflections_AverageSquaredBuffer0[half_pos] = float4(sqrt(max(0.0f, reflection_avg_sqr_0)), 1.0f);
}

// -------------------------------------------------------------
// Resolve Reflections Atrous Iter Pass
// -------------------------------------------------------------
[numthreads(8, 8, 1)]
void ResolveReflections_Atrous_Iter(uint2 did : SV_DispatchThreadID)
{
    int2 half_pos = int2(did);
    if (any(half_pos >= GlossyReflections_HalfRes()))
        return;

    int2 full_pos = GlossyReflections_HalfToFullRes(half_pos);
    if (any(full_pos >= GlossyReflections_FullRes()))
        return;

    float depth = Depth.Load(int3(full_pos, 0));
    if (depth >= 1.0f)
        return;

    float4 normRough = NormalRoughness.Load(int3(full_pos, 0));
    float roughness = normRough.w;

    if (roughness > GlossyReflectionsCB.high_roughness_threshold)
        return;

    float4 inRefl = (GlossyAtrousCB.ping_pong == 0)
        ? g_GlossyReflections_ReflectionsBuffer0[half_pos]
        : g_GlossyReflections_ReflectionsBuffer1[half_pos];

    if (GlossyAtrousCB.ping_pong == 0)
    {
        g_GlossyReflections_ReflectionsBuffer1[half_pos] = inRefl;
        g_GlossyReflections_AverageSquaredBuffer1[half_pos] = g_GlossyReflections_AverageSquaredBuffer0[half_pos];
    }
    else
    {
        g_GlossyReflections_ReflectionsBuffer0[half_pos] = inRefl;
        g_GlossyReflections_AverageSquaredBuffer0[half_pos] = g_GlossyReflections_AverageSquaredBuffer1[half_pos];
    }
}

// -------------------------------------------------------------
// Resolve Reflections Atrous Last Pass (Full Res output)
// -------------------------------------------------------------
[numthreads(8, 8, 1)]
void ResolveReflections_Atrous_Last(uint2 did : SV_DispatchThreadID)
{
    int2 full_pos = int2(did);
    if (any(full_pos >= GlossyReflections_FullRes()))
        return;

    float depth = Depth.Load(int3(full_pos, 0));
    if (depth >= 1.0f)
    {
        g_ReflectionBuffer[full_pos] = 0.0f;
        g_GlossyReflections_StandardDevBuffer[full_pos] = 0.0f;
        return;
    }

    float4 normRough = NormalRoughness.Load(int3(full_pos, 0));
    float roughness = normRough.w;

    if (roughness > GlossyReflectionsCB.high_roughness_threshold)
    {
        g_ReflectionBuffer[full_pos] = 0.0f;
        g_GlossyReflections_StandardDevBuffer[full_pos] = 0.0f;
        return;
    }

    int2 half_pos = GlossyReflections_FullToHalfRes(full_pos);
    float4 reflection = g_GlossyReflections_ReflectionsBuffer1[half_pos];
    if (dot(reflection.xyz, reflection.xyz) == 0.0f)
    {
        reflection = g_GlossyReflections_ReflectionsBuffer0[half_pos];
    }
    if (dot(reflection.xyz, reflection.xyz) == 0.0f)
    {
        reflection = g_GlossyReflections_SpecularBuffer[half_pos];
    }

    float4 avg_sqr = g_GlossyReflections_AverageSquaredBuffer1[half_pos];
    float3 stdDev = sqrt(max(0.0f, avg_sqr.xyz * avg_sqr.xyz - reflection.xyz * reflection.xyz));

    // Inverse tone map back
    reflection.xyz /= max(1.0f - reflection.xyz, 1e-3f);
    stdDev /= max(1.0f - stdDev, 1e-3f);

    g_ReflectionBuffer[full_pos] = reflection;
    g_GlossyReflections_StandardDevBuffer[full_pos] = float4(stdDev, 1.0f);
}

// -------------------------------------------------------------
// Mark Fireflies Kernel
// -------------------------------------------------------------
[numthreads(8, 8, 1)]
void MarkFireflies(uint2 did : SV_DispatchThreadID)
{
    int2 half_pos = int2(did);
    if (any(half_pos >= GlossyReflections_HalfRes()))
        return;

    float4 spec = g_GlossyReflections_SpecularBuffer[half_pos];
    float lum = Luminance(spec.xyz);

    float sumLum = 0.0f;
    float count = 0.0f;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0) continue;
            int2 p = clamp(half_pos + int2(x, y), 0, GlossyReflections_HalfRes() - 1);
            sumLum += Luminance(g_GlossyReflections_SpecularBuffer[p].xyz);
            count += 1.0f;
        }
    }

    float avgLum = sumLum / max(count, 1.0f);
    g_GlossyReflections_FirefliesBuffer[half_pos] = (lum > avgLum * 4.0f && lum > 2.0f) ? 1.0f : 0.0f;
}

// -------------------------------------------------------------
// Cleanup Fireflies Kernel
// -------------------------------------------------------------
[numthreads(8, 8, 1)]
void CleanupFireflies(uint2 did : SV_DispatchThreadID)
{
    int2 half_pos = int2(did);
    if (any(half_pos >= GlossyReflections_HalfRes()))
        return;

    float isFirefly = g_GlossyReflections_FirefliesBuffer[half_pos];
    if (isFirefly > 0.5f)
    {
        float4 sum = 0.0f;
        float count = 0.0f;

        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0) continue;
                int2 p = clamp(half_pos + int2(x, y), 0, GlossyReflections_HalfRes() - 1);
                if (g_GlossyReflections_FirefliesBuffer[p] < 0.5f)
                {
                    sum += g_GlossyReflections_SpecularBuffer[p];
                    count += 1.0f;
                }
            }
        }

        if (count > 0.0f)
        {
            g_GlossyReflections_SpecularBuffer[half_pos] = sum / count;
        }
    }
}

// -------------------------------------------------------------
// Reproject Reflections Kernel
// -------------------------------------------------------------
[numthreads(8, 8, 1)]
void ReprojectReflections(uint2 did : SV_DispatchThreadID)
{
    int2 full_pos = int2(did);
    if (any(full_pos >= GlossyReflections_FullRes()))
        return;

    float depth = Depth.Load(int3(full_pos, 0));
    if (depth >= 1.0f)
        return;

    float4 curRefl = g_ReflectionBuffer[full_pos];
    float hitDist = curRefl.w;

    float2 uv = (float2(full_pos) + 0.5f) / float2(GlossyReflections_FullRes());
    float2 motion = MotionVectors.Load(int3(full_pos, 0)).xy;
    float2 prevUV = uv - motion;

    if (all(prevUV >= 0.0f) && all(prevUV <= 1.0f))
    {
        float4 prevRefl = g_PreviousReflectionBuffer.SampleLevel(LinearClampSampler, prevUV, 0.0f);
        float prevDepth = g_PreviousDepthBuffer.SampleLevel(PointClampSampler, prevUV, 0.0f);

        if (abs(depth - prevDepth) < 0.05f * depth)
        {
            g_ReflectionBuffer[full_pos] = lerp(prevRefl, curRefl, 0.1f);
        }
    }
}
