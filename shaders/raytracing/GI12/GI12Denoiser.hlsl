/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
MIT License
********************************************************************/

#include "GI12Registers.hlsli"
#include "GI12Common.hlsli"

#define kGIDenoiser_MaxBlurMask 8.0f

// GIDenoiser UAVs
RWTexture2D<float>   g_GIDenoiser_BlurMask                 : register(u0);
RWTexture2D<float4>  g_GIDenoiser_ColorBuffer              : register(u1);
RWTexture2D<float>   g_GIDenoiser_ColorDeltaBuffer         : register(u2);
RWTexture2D<float4>  g_IrradianceBuffer                    : register(u3);
RWTexture2D<float4>  g_MainOutput                          : register(u4);

// GIDenoiser SRVs
Texture2D<float4>    g_GIDenoiser_PreviousColorBuffer      : register(t11);
Texture2D<float>     g_GIDenoiser_PreviousColorDeltaBuffer : register(t12);
Texture2D<float>     g_PreviousDepthBuffer                 : register(t13);
Texture2D<snorm float4> g_PreviousNormalBuffer             : register(t14);
Texture2D<float4>    g_ReflectionBuffer                    : register(t15);

float3 ScreenToWorld(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(GI12CB.view_proj_inv, clip);
    return world.xyz / world.w + GI12CB.eye;
}

float3 ScreenToWorldPrev(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(GI12CB.view_proj_inv_prev, clip);
    return world.xyz / world.w + GI12CB.eye_prev;
}

float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

int GIDenoiser_GetBlurRadius(uint2 pos)
{
    float blurMask = g_GIDenoiser_BlurMask[pos] * kGIDenoiser_MaxBlurMask;
    if (blurMask > 0.0f)
    {
        return int(max(blurMask, 1.0f) + 0.5f);
    }
    return 0;
}

float3 RemoveNaNs(float3 color)
{
    color /= (1.0f + color);
    color = saturate(color);
    color /= max(1.0f - color, 1e-4f);
    return color;
}

float4 RemoveNaNs(float4 color)
{
    color /= (1.0f + color);
    color = saturate(color);
    color /= max(1.0f - color, 1e-4f);
    return color;
}

// -------------------------------------------------------------
// Reproject GI Kernel
// -------------------------------------------------------------
[numthreads(8, 8, 1)]
void ReprojectGI(uint2 did : SV_DispatchThreadID)
{
    if (any(did >= GI12CB.buffer_dimensions))
        return;

    float depth = Depth.Load(int3(did, 0));
    if (depth >= 1.0f)
    {
        g_GIDenoiser_BlurMask[did] = 0.0f;
        g_GIDenoiser_ColorBuffer[did] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_GIDenoiser_ColorDeltaBuffer[did] = 0.0f;
        return;
    }

    float4 color = g_GIDenoiser_ColorBuffer[did];
    float4 lighting = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 normal = normalize(NormalRoughness.Load(int3(did, 0)).xyz);

    float alphaBlend = 1.0f;
    float colorDelta = 0.0f;
    float2 uv = (float2(did) + 0.5f) / float2(GI12CB.buffer_dimensions);

    float2 velocity = MotionVectors.Load(int3(did, 0)).xy;
    float2 previousUV = uv - velocity;

    if (all(previousUV > 0.0f) && all(previousUV < 1.0f))
    {
        float3 world = ScreenToWorld(uv, depth);
        float cellSize = distance(GI12CB.eye, world) * ScreenProbesCB.cell_size;
        cellSize *= lerp(1.0f, 5.0f, pow(1.0f - max(dot(normalize(GI12CB.eye - world), normal), 0.0f), 6.0f));

        float weight = 0.0f;
        float2 texelSize = 1.0f / float2(GI12CB.buffer_dimensions);
        const float kOneOverSqrtOfTwo = 0.707107f;

        for (float y = -1.0f; y <= 1.0f; ++y)
        {
            for (float x = -1.0f; x <= 1.0f; ++x)
            {
                float2 st = previousUV + float2(x, y) * texelSize;
                float4 c = g_GIDenoiser_PreviousColorBuffer.SampleLevel(PointClampSampler, st, 0.0f);
                float3 prevNormal = normalize(g_PreviousNormalBuffer.SampleLevel(PointClampSampler, st, 0.0f).xyz);

                if (c.w < 1.0f || dot(prevNormal, prevNormal) == 0.0f)
                    continue;

                float prevDepth = g_PreviousDepthBuffer.SampleLevel(PointClampSampler, st, 0.0f).x;
                float3 prevWorld = ScreenToWorldPrev(st, prevDepth);

                if (distance(world, prevWorld) < cellSize && dot(normal, prevNormal) > 0.95f)
                {
                    float subpixelDist = distance(floor(st * float2(GI12CB.buffer_dimensions)) + 0.5f, previousUV * float2(GI12CB.buffer_dimensions));
                    float w = saturate(1.0f - subpixelDist * kOneOverSqrtOfTwo);

                    colorDelta += w * g_GIDenoiser_PreviousColorDeltaBuffer.SampleLevel(PointClampSampler, st, 0.0f).x;
                    lighting += w * c;
                    weight += w;
                }
            }
        }

        if (weight > 0.0f)
        {
            float w = sign(colorDelta);
            colorDelta = abs(colorDelta) / weight;
            colorDelta *= w;
            lighting = RemoveNaNs(lighting / weight);
        }

        if (color.w > 0.0f)
        {
            float lumaA = Luminance(color.xyz);
            float lumaB = Luminance(lighting.xyz / max(lighting.w, 1.0f));
            colorDelta = lerp(colorDelta, lumaA - lumaB, 1.0f / 8.0f);
            alphaBlend = saturate(1.0f - abs(colorDelta) / max(lumaB, 1e-4f));
        }
    }

    float blurMask = max(kGIDenoiser_MaxBlurMask - lighting.w, 0.0f);

    if (color.w > 0.0f || lighting.w < 1.0f)
    {
        lighting += float4(color.xyz, color.w > 0.0f ? 1.0f : -1.0f);
    }

    float2 vignetteUV = uv * (1.0f - uv.yx);
    float vignette = pow(15.0f * vignetteUV.x * vignetteUV.y, 0.25f);
    float maxSampleCount = max(lerp(4.0f, 8.0f * min(abs(lighting.w), kGIDenoiser_MaxBlurMask), alphaBlend) * vignette, 1.0f);

    if (lighting.w > maxSampleCount)
    {
        lighting *= (maxSampleCount / lighting.w);
    }

    g_GIDenoiser_BlurMask[did] = blurMask / kGIDenoiser_MaxBlurMask;
    g_GIDenoiser_ColorBuffer[did] = lighting;
    g_GIDenoiser_ColorDeltaBuffer[did] = colorDelta;
}

// -------------------------------------------------------------
// Filter GI Kernel (Separable Bilateral Filter)
// -------------------------------------------------------------
[numthreads(8, 8, 1)]
void FilterGI(uint2 did : SV_DispatchThreadID)
{
    if (any(did >= GI12CB.buffer_dimensions))
        return;

    float4 lighting = g_GIDenoiser_ColorBuffer.Load(int3(did, 0));
    int blurRadius = GIDenoiser_GetBlurRadius(did);

    if (blurRadius > 0)
    {
        float weight = 1.0f;
        float3 color = lighting.xyz / max(lighting.w, 1.0f);

        float centerDepth = Depth.Load(int3(did, 0));
        float3 centerNormal = normalize(NormalRoughness.Load(int3(did, 0)).xyz);

        int2 blurDir = int2(GIDenoiserCB.blur_direction_x, GIDenoiserCB.blur_direction_y);
        for (int r = -blurRadius; r <= blurRadius; ++r)
        {
            if (r == 0) continue;

            int2 pos = clamp(int2(did) + r * blurDir, 0, int2(GI12CB.buffer_dimensions) - 1);
            float4 c = g_GIDenoiser_ColorBuffer.Load(int3(pos, 0));

            if (c.w > 0.0f)
            {
                float depth = Depth.Load(int3(pos, 0));
                float3 normal = normalize(NormalRoughness.Load(int3(pos, 0)).xyz);

                float depthDiff = abs(centerDepth - depth) / max(centerDepth, 1e-4f);
                float depthFactor = exp2(-20.0f * depthDiff);
                float normalFactor = max(dot(normal, centerNormal), 0.0f);
                normalFactor *= normalFactor; normalFactor *= normalFactor;

                float w = depthFactor * normalFactor;
                color += w * (c.xyz / max(c.w, 1.0f));
                weight += w;
            }
        }

        lighting.xyz = (color / weight) * max(lighting.w, 1.0f);
    }

    if (GIDenoiserCB.blur_direction_y > 0)
    {
        g_IrradianceBuffer[did] = float4(lighting.xyz / max(lighting.w, 1.0f), 1.0f);
    }

    g_GIDenoiser_ColorBuffer[did] = lighting;
}

[numthreads(8, 8, 1)]
void CompositeGI(uint2 did : SV_DispatchThreadID)
{
    if (any(did >= GI12CB.buffer_dimensions))
        return;

    float depth = Depth.Load(int3(did, 0));
    if (depth >= 1.0f)
    {
        float3 sky = SkyHemisphere.SampleLevel(LinearClampSampler, float2(0.5f, 0.5f), 0).xyz * Raytracing.Sky;
        g_MainOutput[did] = float4(sky, 1.0f);
        return;
    }

    float3 albedo = Albedo.Load(int3(did, 0)).xyz;
    float3 irradiance = g_IrradianceBuffer[did].xyz;
    float3 specular = g_ReflectionBuffer.Load(int3(did, 0)).xyz;

    float4 normRough = NormalRoughness.Load(int3(did, 0));
    float roughness = normRough.w;
    float metalness = 0.04f;

    float2 uv = (float2(did) + 0.5f) / float2(GI12CB.buffer_dimensions);
    float3 worldPos = ScreenToWorld(uv, depth);
    float3 viewDir = normalize(GI12CB.eye - worldPos);
    float3 normal = normalize(normRough.xyz);
    float NdotV = saturate(dot(normal, viewDir));
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);
    float3 F = F0 + (1.0f - F0) * pow(saturate(1.0f - NdotV), 5.0f);

    float3 diffuse = albedo * irradiance * (1.0f / PI) * (1.0f - metalness);
    float3 finalColor = diffuse + specular * F;
    g_MainOutput[did] = float4(finalColor, 1.0f);
}
