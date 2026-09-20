#ifndef ASVGF_COMMON_HLSLI
#define ASVGF_COMMON_HLSLI

// 1D discrete B3-spline kernel weights for 5x5 filter
static const float ASVGF_Kernel1D[5] = { 1.0f / 16.0f, 1.0f / 4.0f, 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f };

// ITU-R BT.709 luminance calculation
inline float ASVGF_Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

// Normal edge-stopping weight using cosine falloff
inline float ASVGF_NormalWeight(float3 nCenter, float3 nSample, float power)
{
    float p = clamp(power, 4.0f, 32.0f);
    return pow(saturate(dot(nCenter, nSample)), p);
}

// Depth edge-stopping weight using relative view depth difference scaled with stepSize
inline float ASVGF_DepthWeight(float zCenter, float zSample, float sigmaZ, float stepSize = 1.0f)
{
    float diff = abs(zCenter - zSample);
    float scale = max(abs(zCenter) * sigmaZ * 0.03f * stepSize, 0.05f * stepSize);
    return exp(-diff / scale);
}

// Luminance edge-stopping weight guided by local standard deviation with a luminance baseline
inline float ASVGF_LuminanceWeight(float lCenter, float lSample, float stdDev, float sigmaL)
{
    float diff = abs(lCenter - lSample);
    float scale = max(stdDev * sigmaL, 0.12f * max(lCenter, lSample) + 0.02f);
    return exp(-diff / scale);
}

#endif
