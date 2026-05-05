#ifndef COLOR_CONVERSIONS_COMMON_HLSLI
#define COLOR_CONVERSIONS_COMMON_HLSLI

#include "interop/SharedData.hlsli"
#include "Utils/MathConstants.hlsli"

#define LLSETTINGS Features.LinearLighting
#define LLON LLSETTINGS.enableLinearLighting

// Attempt to match vanilla materials that are darker than PBR
const static float PBRLightingScale = LLON ? 1.0f : 0.65f;
	
const static float PBRLightingScaleRcp = 1.0f / PBRLightingScale;

const static float PBRLightingCompensation = LLON ? 1.0 : K_PI;

float3 PBRColorScale(float3 color)
{
    return color * PBRLightingScale;
}

float4 PBRColorScale(float4 color)
{
    return float4(PBRColorScale(color.rgb), color.a);
}

float3 ColorToGamma(float3 color)
{
    return pow(abs(color), 1.0f / (LLON ? LLSETTINGS.colorGamma : 2.2f));
}

float3 ColorToLinear(float3 color)
{
    return pow(abs(color), (LLON ? LLSETTINGS.colorGamma : 2.2f));
}

float3 EffectToLinear(float3 color)
{
    return pow(abs(color), (LLON ? LLSETTINGS.effectGamma : 2.2f)) * (LLON ? LLSETTINGS.effectLightingMult : 1.0);
}

float3 LightToLinear(float3 color)
{
    return pow(abs(color), (LLON ? LLSETTINGS.lightGamma : 2.2f));
}

float3 PointLightToLinear(float3 color, bool isLinear)
{
    float mult = LLON ? (isLinear ? LLSETTINGS.pointLightMult : 1.0f) : K_PI;
    float3 finalColor = (isLinear && LLON) ? color : LightToLinear(color);
    return finalColor * mult;
}

float3 DirLightToLinear(float3 color)
{
    float mult = LLON ? (LLSETTINGS.isDirLightLinear ? LLSETTINGS.directionalLightMult * LLSETTINGS.dirLightMult : 1.0f) : K_PI;
    float3 finalColor = (LLSETTINGS.isDirLightLinear && LLON) ? color : LightToLinear(color);
    return finalColor * mult;
}

float3 GlowToLinear(float3 color)
{
    return LLON ? pow(abs(color), LLSETTINGS.glowmapGamma) * LLSETTINGS.glowmapMult : color;
}

float VanillaDiffuseColorMult()
{
    return LLON ? LLSETTINGS.vanillaDiffuseColorMult : PBRLightingScaleRcp;
}

float3 VanillaDiffuseColor(float3 color)
{
    return ColorToLinear(color) * VanillaDiffuseColorMult();
}

float4 VanillaDiffuseColor(float4 color)
{
    return float4(VanillaDiffuseColor(color.rgb), color.a);
}

float3 VanillaDiffuseColorGamma(float3 color)
{
    return ColorToGamma(color / VanillaDiffuseColorMult());
}

float3 LLGammaToTrueLinear(float3 color)
{
    return LLON ? color : pow(abs(color), 2.2f);
}

float3 LLTrueLinearToGamma(float3 color)
{
    return LLON ? color : pow(abs(color), 1.0f / 2.2f);
}

float3 EmitColorToLinear(float3 color)
{
    return pow(abs(color), LLON ? LLSETTINGS.emitColorGamma : 2.2f);
}

float EmitColorMult()
{
    return LLON ? LLSETTINGS.emitColorMult : 1.0f;
}
#endif