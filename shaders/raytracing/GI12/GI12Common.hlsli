/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
MIT License
********************************************************************/

#ifndef GI12_COMMON_HLSLI
#define GI12_COMMON_HLSLI

#include "interop/GI12Data.hlsli"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#ifndef TWO_PI
#define TWO_PI 6.28318530717958647692f
#endif

#define kGI1_InvalidId 0xFFFFFFFFu
#define kGI1_AngleThreshold cos(2e-2f * PI)
#define kGI1_FloatQuantize 1e4f

#define kHashGridCache_TileDecay 50
#define kHashGridCache_FloatQuantize 1e3f
#define HASHGRIDCACHE_STEP_FACTOR 1e3f
#define HASHGRIDCACHE_SIZE_FACTOR 1e-3f

// -------------------------------------------------------------
// Packing / Unpacking
// -------------------------------------------------------------

uint packHalf2(float2 value)
{
    return f32tof16(value.x) | (f32tof16(value.y) << 16);
}

float2 unpackHalf2(uint packedValue)
{
    return float2(f16tof32(packedValue & 0xFFFFu), f16tof32(packedValue >> 16));
}

uint2 packHalf4(float4 value)
{
    return uint2(f32tof16(value.x) | (f32tof16(value.y) << 16),
                 f32tof16(value.z) | (f32tof16(value.w) << 16));
}

float4 unpackHalf4(uint2 packedValue)
{
    return float4(f16tof32(packedValue.x & 0xFFFFu), f16tof32(packedValue.x >> 16),
                  f16tof32(packedValue.y & 0xFFFFu), f16tof32(packedValue.y >> 16));
}

uint2 packHalf3(float3 value)
{
    return packHalf4(float4(value, 0.0f));
}

float3 unpackHalf3(uint2 packedValue)
{
    return unpackHalf4(packedValue).xyz;
}

uint packUnorm2x16(float2 value)
{
    uint2 packedValue = uint2(saturate(value) * 65535.0f + 0.5f);
    return packedValue.x | (packedValue.y << 16);
}

float2 unpackUnorm2x16(uint packedValue)
{
    uint2 value = uint2(packedValue, packedValue >> 16) & 0xFFFFu;
    return float2(value) * (1.0f / 65535.0f);
}

uint packNormal(float3 value)
{
    int3 packedValue = int3(clamp(value, -1.0f, 1.0f) * 511.0f + (0.5f * sign(value))) & 0x3FFu;
    return packedValue.x | (packedValue.y << 10) | (packedValue.z << 20);
}

float3 unpackNormal(uint packedValue)
{
    int3 value = int3(packedValue << 22, packedValue << 12, packedValue << 2) >> 22;
    return float3(value) * (1.0f / 511.0f);
}

// -------------------------------------------------------------
// Halton Sequences & Hashing
// -------------------------------------------------------------

float CalculateHaltonNumber(in uint index, in uint base)
{
    float f = 1.0f;
    float result = 0.0f;
    for (uint i = index; i > 0;)
    {
        f /= float(base);
        result = result + f * float(i % base);
        i = uint(i / float(base));
    }
    return result;
}

float2 CalculateHaltonSequence(in uint index)
{
    return float2(CalculateHaltonNumber((index & 0xFFu) + 1, 2),
                  CalculateHaltonNumber((index & 0xFFu) + 1, 3));
}

uint pcgHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint xxHash(uint input)
{
    uint h = input + 374761393u;
    h = (h ^ (h >> 13u)) * 127412617u;
    return h ^ (h >> 16u);
}

float rand_pcg(inout uint state)
{
    state = pcgHash(state);
    return float(state) * (1.0f / 4294967296.0f);
}

float ConvertPerceptualRoughnessToAlpha(float perceptualRoughness)
{
    return perceptualRoughness * perceptualRoughness;
}

float ClampAlphaRoughness(float alpha)
{
    return clamp(alpha, 0.001f, 1.0f);
}

// -------------------------------------------------------------
// Spherical Harmonics
// -------------------------------------------------------------

void SH_GetCoefficients(in float3 direction, out float coefficients[9])
{
    float pz2 = direction.z * direction.z;
    coefficients[0] = 0.2820947917738781f;
    coefficients[2] = 0.4886025119029199f * direction.z;
    coefficients[6] = 0.9461746957575601f * pz2 + -0.3153915652525201f;
    float fC0 = direction.x;
    float fS0 = direction.y;
    float fTmpA = -0.48860251190292f;
    coefficients[3] = fTmpA * fC0;
    coefficients[1] = fTmpA * fS0;
    float fTmpB = -1.092548430592079f * direction.z;
    coefficients[7] = fTmpB * fC0;
    coefficients[5] = fTmpB * fS0;
    float fC1 = direction.x * fC0 - direction.y * fS0;
    float fS1 = direction.x * fS0 + direction.y * fC0;
    float fTmpC = 0.5462742152960395f;
    coefficients[8] = fTmpC * fC1;
    coefficients[4] = fTmpC * fS1;
}

void SH_GetCoefficients_ClampedCosine(in float3 cosine_lobe_dir, out float coefficients[9])
{
    const float CosineA0 = PI;
    const float CosineA1 = (2.0f * PI) / 3.0f;
    const float CosineA2 = PI / 4.0f;
    float pz2 = cosine_lobe_dir.z * cosine_lobe_dir.z;
    coefficients[0] = 0.2820947917738781f * CosineA0;
    coefficients[2] = 0.4886025119029199f * cosine_lobe_dir.z * CosineA1;
    coefficients[6] = 0.7431238683011271f * pz2 + -0.2477079561003757f;
    float fC0 = cosine_lobe_dir.x;
    float fS0 = cosine_lobe_dir.y;
    float fTmpA = -0.48860251190292f;
    coefficients[3] = fTmpA * fC0 * CosineA1;
    coefficients[1] = fTmpA * fS0 * CosineA1;
    float fTmpB = -1.092548430592079f * cosine_lobe_dir.z;
    coefficients[7] = fTmpB * fC0 * CosineA2;
    coefficients[5] = fTmpB * fS0 * CosineA2;
    float fC1 = cosine_lobe_dir.x * fC0 - cosine_lobe_dir.y * fS0;
    float fS1 = cosine_lobe_dir.x * fS0 + cosine_lobe_dir.y * fC0;
    float fTmpC = 0.5462742152960395f;
    coefficients[8] = fTmpC * fC1 * CosineA2;
    coefficients[4] = fTmpC * fS1 * CosineA2;
}

void SH_GetCoefficients_ClampedCosine_Cone(in float3 cosine_lobe_dir, in float cone_theta_max, out float coefficients[9])
{
    float sin_theta_max, cos_theta_max;
    sincos(cone_theta_max, sin_theta_max, cos_theta_max);
    float sin_theta_max2 = sin_theta_max * sin_theta_max;
    float sin_theta_max3 = sin_theta_max2 * sin_theta_max;
    float cos_theta_max2 = cos_theta_max * cos_theta_max;
    float cos_theta_max3 = cos_theta_max2 * cos_theta_max;

    float band1_factor = 1.023326707946489f * (1.0f - cos_theta_max3);
    float band2_factor = (4.0f - 3.0f * sin_theta_max3) * sin_theta_max2;

    coefficients[0] = 0.886226925452758f * sin_theta_max2;
    coefficients[1] = -band1_factor * cosine_lobe_dir.y;
    coefficients[2] = +band1_factor * cosine_lobe_dir.z;
    coefficients[3] = -band1_factor * cosine_lobe_dir.x;
    coefficients[4] = +0.8580855308097834f * band2_factor * cosine_lobe_dir.x * cosine_lobe_dir.y;
    coefficients[5] = -0.8580855308097834f * band2_factor * cosine_lobe_dir.y * cosine_lobe_dir.z;
    coefficients[6] = +0.2477079561003757f * band2_factor * (3.0f * cosine_lobe_dir.z * cosine_lobe_dir.z - 1.0f);
    coefficients[7] = -0.8580855308097834f * band2_factor * cosine_lobe_dir.x * cosine_lobe_dir.z;
    coefficients[8] = +0.4290427654048917f * band2_factor * (cosine_lobe_dir.x * cosine_lobe_dir.x - cosine_lobe_dir.y * cosine_lobe_dir.y);
}

// -------------------------------------------------------------
// Screen Probes Helpers
// -------------------------------------------------------------

uint ScreenProbes_PackSeed(in uint2 seed)
{
    return (seed.x << 16) | seed.y;
}

uint2 ScreenProbes_UnpackSeed(in uint packed_seed)
{
    return uint2(packed_seed >> 16, packed_seed & 0xFFFFu);
}

uint2 ScreenProbes_PackSHColor(in float4 sh_color)
{
    return packHalf4(sh_color);
}

float4 ScreenProbes_UnpackSHColor(in uint2 packed_sh_color)
{
    return unpackHalf4(packed_sh_color);
}

uint2 ScreenProbes_PackRadiance(in float4 radiance)
{
    return packHalf4(radiance);
}

float4 ScreenProbes_UnpackRadiance(in uint2 packed_radiance)
{
    return unpackHalf4(packed_radiance);
}

uint2 HashGridCache_PackRadiance(in float4 radiance)
{
    return packHalf4(radiance);
}

float4 HashGridCache_UnpackRadiance(in uint2 packed_radiance)
{
    return unpackHalf4(packed_radiance);
}

uint4 ScreenProbes_QuantizeRadiance(in float4 radiance)
{
    return uint4(round(kGI1_FloatQuantize * radiance));
}

float4 ScreenProbes_RecoverRadiance(in uint4 quantized_radiance)
{
    return float4(quantized_radiance) / kGI1_FloatQuantize;
}

uint2 ScreenProbes_PackSample(in float3 direction)
{
    return packHalf3(direction);
}

float3 ScreenProbes_UnpackSample(in uint2 packed_sample)
{
    return unpackHalf3(packed_sample);
}

// -------------------------------------------------------------
// Reservoir Structures
// -------------------------------------------------------------

struct Reservoir
{
    float y;
    float w_sum;
    float M;
    float W;
};

Reservoir makeReservoir()
{
    Reservoir r;
    r.y = 0.0f;
    r.w_sum = 0.0f;
    r.M = 0.0f;
    r.W = 0.0f;
    return r;
}

bool updateReservoir(inout Reservoir r, float candidate_y, float weight, float random_val)
{
    r.w_sum += weight;
    r.M += 1.0f;
    if (random_val * r.w_sum < weight)
    {
        r.y = candidate_y;
        return true;
    }
    return false;
}

uint4 packReservoir(in Reservoir r)
{
    return uint4(asuint(r.y), asuint(r.w_sum), asuint(r.M), asuint(r.W));
}

Reservoir unpackReservoir(in uint4 data)
{
    Reservoir r;
    r.y = asfloat(data.x);
    r.w_sum = asfloat(data.y);
    r.M = asfloat(data.z);
    r.W = asfloat(data.w);
    return r;
}

// -------------------------------------------------------------
// Glossy Reflections Helpers
// -------------------------------------------------------------

uint GlossyReflections_PackSample(in uint2 seed)
{
    return ((seed.y & 0xFFFFu) << 0) | ((seed.x & 0xFFFFu) << 16);
}

uint2 GlossyReflections_UnpackSample(in uint packed_sample)
{
    return uint2((packed_sample >> 16) & 0xFFFFu, (packed_sample >> 0) & 0xFFFFu);
}

float AtrousRatioEstimator_GaussianFilter(float x, float step)
{
    float sigma = 1.065f * max(step, 1.0f);
    return (max(step, 1.0f) / (sqrt(2.0f * PI) * sigma)) * exp(-(x * x) / (2.0f * sigma * sigma));
}

float sampleGGXPDF(float roughnessAlpha, float roughnessAlphaSqr, float dotNH, float dotNV, float3 localView)
{
    float d = roughnessAlphaSqr / (PI * pow(dotNH * dotNH * (roughnessAlphaSqr - 1.0f) + 1.0f, 2.0f));
    float dotNV2 = saturate(dotNV * dotNV);
    float s = roughnessAlphaSqr * (1.0f - dotNV2);
    float t = sqrt(s + dotNV2);
    return d / (2.0f * max(t + dotNV, 1e-5f));
}

#endif // GI12_COMMON_HLSLI
