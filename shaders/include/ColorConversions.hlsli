#ifndef COLOR_CONVERSIONS_COMMON_HLSLI
#define COLOR_CONVERSIONS_COMMON_HLSLI

#include "interop/SharedData.hlsli"
#include "Utils/MathConstants.hlsli"

#define LLSETTINGS Features.LinearLighting
#define LLON LLSETTINGS.enableLinearLighting

// Light multiplier to match vanilla raster
#define DIRECTIONAL_LIGHT_MULTIPLIER (K_4PI)
#define POINT_LIGHT_MULTIPLIER (K_PI)

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
    float mult = LLON ? (isLinear ? LLSETTINGS.pointLightMult : 1.0f) : POINT_LIGHT_MULTIPLIER;   
    float3 finalColor = (isLinear && LLON) ? color : LightToLinear(color);
    return finalColor * mult;
}

float3 DirLightToLinear(float3 color)
{
    float mult = LLON ? (LLSETTINGS.isDirLightLinear ? LLSETTINGS.directionalLightMult * LLSETTINGS.dirLightMult : 1.0f) : DIRECTIONAL_LIGHT_MULTIPLIER;
    float3 finalColor = (LLSETTINGS.isDirLightLinear && LLON) ? color : LightToLinear(color);
    return finalColor * mult;
}

float3 GlowToLinear(float3 color)
{
    return LLON ? pow(abs(color), LLSETTINGS.glowmapGamma) * LLSETTINGS.glowmapMult : color;
}

float3 VanillaDiffuseColor(float3 color)
{
    return saturate(ColorToLinear(color) * LLSETTINGS.vanillaDiffuseColorMult);
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
    return LLON ? (pow(abs(color), LLSETTINGS.emitColorGamma)) : color;
}

float EmitColorMult()
{
    return LLON ? LLSETTINGS.emitColorMult : 1.0f;
}
#endif