#ifndef PARALLAX_HLSLI
#define PARALLAX_HLSLI

#include "include/Common.hlsli"

struct DisplacementParams
{
    float DisplacementScale;
    float DisplacementOffset;
    float HeightScale;
    float FlattenAmount;
};

namespace ExtendedMaterials
{
    static const float ShadowIntensity = 2.0;

    float ScaleDisplacement(float displacement, DisplacementParams params)
    {
        return (displacement - 0.5) * params.HeightScale;
    }

    float AdjustDisplacementNormalized(float displacement, DisplacementParams params)
    {
        return (displacement - 0.5) * params.DisplacementScale + 0.5 + params.DisplacementOffset;
    }

    float4 AdjustDisplacementNormalized(float4 displacement, DisplacementParams params)
    {
        return float4(AdjustDisplacementNormalized(displacement.x, params), AdjustDisplacementNormalized(displacement.y, params), AdjustDisplacementNormalized(displacement.z, params), AdjustDisplacementNormalized(displacement.w, params));
    }

    float2 ParallaxRaymarch(
        float2 coords,
        float mipLevel,
        float2 viewDirTS,
        float nearBlendToFar,
        float maxHeight,
        float minHeight,
        Texture2D<float4> tex,
        SamplerState texSampler,
        uint channel,
        DisplacementParams params,
        out float pixelOffset)
    {
        // CS uses 16, but raytracing beign noisy by default allows us to use less steps
        const float maxSteps = 8;

        uint numSteps = uint((maxSteps * (1.0 - nearBlendToFar)) + 0.5);
        numSteps = clamp(numSteps, 4, max(4, uint(params.HeightScale * maxSteps)));
        numSteps = (numSteps + 2) & ~3;

        float stepSize = rcp(numSteps);

        float2 offsetPerStep = viewDirTS * (maxHeight * stepSize);
        float2 prevOffset = viewDirTS * minHeight + coords;

        float prevBound = 1.0;
        float prevHeight = 1.0;

        float2 pt1 = 0;
        float2 pt2 = 0;

        uint numStepsTemp = numSteps;
        bool contactRefinement = false;

        [loop]
        while (numSteps > 0)
        {
            float4 currentOffset[2];
            currentOffset[0] = prevOffset.xyxy - float4(1,1,2,2) * offsetPerStep.xyxy;
            currentOffset[1] = prevOffset.xyxy - float4(3,3,4,4) * offsetPerStep.xyxy;

            float4 currentBound = prevBound.xxxx - float4(1,2,3,4) * stepSize;

            float4 currHeight;
            currHeight.x = tex.SampleLevel(texSampler, currentOffset[0].xy, mipLevel)[channel];
            currHeight.y = tex.SampleLevel(texSampler, currentOffset[0].zw, mipLevel)[channel];
            currHeight.z = tex.SampleLevel(texSampler, currentOffset[1].xy, mipLevel)[channel];
            currHeight.w = tex.SampleLevel(texSampler, currentOffset[1].zw, mipLevel)[channel];

            currHeight = AdjustDisplacementNormalized(currHeight, params);

            bool4 testResult = currHeight >= currentBound;

            [branch]
            if (any(testResult))
            {
                float2 outOffset = 0;

                [flatten]
                if (testResult.w)
                {
                    outOffset = currentOffset[1].xy;
                    pt1 = float2(currentBound.w, currHeight.w);
                    pt2 = float2(currentBound.z, currHeight.z);
                }

                [flatten]
                if (testResult.z)
                {
                    outOffset = currentOffset[0].zw;
                    pt1 = float2(currentBound.z, currHeight.z);
                    pt2 = float2(currentBound.y, currHeight.y);
                }

                [flatten]
                if (testResult.y)
                {
                    outOffset = currentOffset[0].xy;
                    pt1 = float2(currentBound.y, currHeight.y);
                    pt2 = float2(currentBound.x, currHeight.x);
                }

                [flatten]
                if (testResult.x)
                {
                    outOffset = prevOffset;
                    pt1 = float2(currentBound.x, currHeight.x);
                    pt2 = float2(prevBound, prevHeight);
                }

                if (contactRefinement)
                {
                    break;
                }

                contactRefinement = true;
                prevOffset = outOffset;
                prevBound = pt2.x;
                numSteps = numStepsTemp;
                stepSize /= (float)numSteps;
                offsetPerStep /= (float)numSteps;
                continue;
            }

            prevOffset = currentOffset[1].zw;
            prevBound = currentBound.w;
            prevHeight = currHeight.w;
            numSteps -= 4;
        }

        float delta2 = pt2.x - pt2.y;
        float delta1 = pt1.x - pt1.y;
        float denominator = delta2 - delta1;

        float parallaxAmount =
            denominator == 0.0
                ? 0.0
                : (pt1.x * delta2 - pt2.x * delta1) / denominator;

        float fade = nearBlendToFar * nearBlendToFar;

        float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
        pixelOffset = saturate(lerp(parallaxAmount, 0.5, fade));

        return lerp(viewDirTS * offset + coords, coords, fade);
    }    
    
    float2 GetParallaxCoords(
        float distance,
        float2 coords,
        float mipLevel,
        float3 viewDir,
        float3x3 tbn,
        float noise,
        Texture2D<float4> tex,
        SamplerState texSampler,
        uint channel,
        DisplacementParams params,
        bool interlayer,
        out float pixelOffset)
    {
        pixelOffset = 0.0;

        float3 viewDirTS = normalize(mul(tbn, viewDir));
        viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + params.FlattenAmount;

        float distSq = dot(distance, distance);
        float nearBlendToFar = smoothstep(1024.0 * 1024.0, 2048.0 * 2048.0, distSq);

        float maxHeight = 0.1 * params.HeightScale;
        float minHeight = maxHeight * 0.5;

        if (!interlayer && nearBlendToFar >= 1.0)
            return coords;

        if (interlayer)
            nearBlendToFar = 0.0;

        return ParallaxRaymarch(
            coords,
            mipLevel,
            viewDirTS.xy,
            nearBlendToFar,
            maxHeight,
            minHeight,
            tex,
            texSampler,
            channel,
            params,
            pixelOffset);
    }
}
#endif  // PARALLAX_HLSLI
