#ifndef COLOR_CONVERSIONS_COMMON_HLSLI
#define COLOR_CONVERSIONS_COMMON_HLSLI

#include "interop/SharedData.hlsli"
#include "Utils/MathConstants.hlsli"

#define LLSETTINGS Features.LinearLighting
#define LLON LLSETTINGS.enableLinearLighting
#define LLACESCG LLSETTINGS.enableACEScg

// sRGB -> XYZ -> AP1 (ACEScg) gamut transform matrices
static const float3x3 sRGB_2_XYZ_MAT = {
    0.4123907993, 0.3575843394, 0.1804807884,
    0.2126390059, 0.7151686788, 0.0721923154,
    0.0193308187, 0.1191947798, 0.9505321522,
};

static const float3x3 XYZ_2_AP1_MAT = {
     1.6410233797, -0.3248032942, -0.2364246952,
    -0.6636628587,  1.6153315917,  0.0167563477,
     0.0117218943, -0.0082844420,  0.9883948585,
};

float3 sRGBToAP1(float3 sRGB)
{
    float3 XYZ = mul(sRGB_2_XYZ_MAT, sRGB);
    return mul(XYZ_2_AP1_MAT, XYZ);
}

// Gamut transform: converts linear sRGB-gamut color to ACEScg when enabled
float3 GamutTransform(float3 linearColor)
{
    return LLACESCG ? sRGBToAP1(linearColor) : linearColor;
}

// Light multiplier to match vanilla raster
#define DIRECTIONAL_LIGHT_MULTIPLIER (K_4PI)
#define POINT_LIGHT_MULTIPLIER (1.0f) // K_PI

#define DIFFUSE_MULTIPLIER (K_PI_2)

float3 ColorToGamma(float3 color)
{
    return pow(abs(color), 1.0f / (LLON ? LLSETTINGS.colorGamma : 2.2f));
}

float3 ColorToLinear(float3 color)
{
    return LLON ? GamutTransform(pow(abs(color), LLSETTINGS.colorGamma)) : pow(abs(color), 2.2f);
}

float3 EffectToLinear(float3 color)
{
    return LLON ? GamutTransform(pow(abs(color), LLSETTINGS.effectGamma)) * LLSETTINGS.effectLightingMult : pow(abs(color), 2.2f);
}

float3 LightToLinear(float3 color)
{
    return LLON ? GamutTransform(pow(abs(color), LLSETTINGS.lightGamma)) : pow(abs(color), 2.2f);
}

float3 PointLightToLinear(float3 color, bool isLinear)
{
    float mult = LLON ? (isLinear ? LLSETTINGS.pointLightMult : 1.0f) : POINT_LIGHT_MULTIPLIER;
    float3 finalColor = (isLinear && LLON) ? GamutTransform(color) : LightToLinear(color);
    return finalColor * mult;
}

float3 DirLightToLinear(float3 color)
{
    float mult = LLON ? (LLSETTINGS.isDirLightLinear ? LLSETTINGS.directionalLightMult * LLSETTINGS.dirLightMult : 1.0f) : DIRECTIONAL_LIGHT_MULTIPLIER;
    float3 finalColor = (LLSETTINGS.isDirLightLinear && LLON) ? GamutTransform(color) : LightToLinear(color);
    return finalColor * mult;
}

float3 GlowToLinear(float3 color)
{
    return LLON ? GamutTransform(pow(abs(color), LLSETTINGS.glowmapGamma)) * LLSETTINGS.glowmapMult : color;
}

float VanillaDiffuseColorMult()
{
    return LLON ? LLSETTINGS.vanillaDiffuseColorMult : DIFFUSE_MULTIPLIER;
}

float3 VanillaDiffuseColor(float3 color)
{
    return LLON ? GamutTransform(pow(abs(color), LLSETTINGS.colorGamma)) * LLSETTINGS.vanillaDiffuseColorMult : ColorToLinear(color) * VanillaDiffuseColorMult();
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
    return LLON ? GamutTransform(pow(abs(color), LLSETTINGS.emitColorGamma)) : pow(abs(color), 2.2f);
}

float EmitColorMult()
{
    return LLON ? LLSETTINGS.emitColorMult : 1.0f;
}
#endif