#ifndef COLOR_CONVERSIONS_COMMON_HLSLI
#define COLOR_CONVERSIONS_COMMON_HLSLI

#include "interop/SharedData.hlsli"
#include "Utils/MathConstants.hlsli"

#define LLSETTINGS Features.LinearLighting
#define LLON LLSETTINGS.enableLinearLighting

#if defined (SKYRIM)
// Attempt to match vanilla materials that are darker than PBR
const static float PBRLightingScale = LLON ? 1.0f : 0.65f;
	
const static float PBRLightingScaleRcp = 1.0f / PBRLightingScale;

const static float PBRLightingCompensation = LLON ? 1.0f : K_PI;

const static float PBRLightingScaleCompensation = PBRLightingScale * PBRLightingCompensation;
#else
const static float PBRLightingScaleCompensation = 1.0f;
#endif

float3 PBRColorScale(float3 color)
{
#if defined (SKYRIM)   
    return color * PBRLightingScale;
#else
    return color;
#endif   
}

float4 PBRColorScale(float4 color)
{
    return float4(PBRColorScale(color.rgb), color.a);
}

float3 ColorToGamma(float3 color)
{
#if defined(SKYRIM)
    return pow(abs(color), 1.0f / (LLON ? LLSETTINGS.colorGamma : 2.2f));
#else
    return color;
#endif 
}

float3 ColorToLinear(float3 color)
{
#if defined(SKYRIM)    
    return pow(abs(color), (LLON ? LLSETTINGS.colorGamma : 2.2f));
#else
    return color;
#endif    
}

float3 EffectToLinear(float3 color)
{
#if defined(SKYRIM)
    return pow(abs(color), (LLON ? LLSETTINGS.effectGamma : 2.2f)) * (LLON ? LLSETTINGS.effectLightingMult : 1.0);
#else
    return color;
#endif 
}

float3 LightToLinear(float3 color)
{
#if defined(SKYRIM)
    return pow(abs(color), (LLON ? LLSETTINGS.lightGamma : 2.2f));
#else
    return color;
#endif 
}

float3 PointLightToLinear(float3 color, bool isLinear)
{
#if defined(SKYRIM)    
    float mult = LLON ? (isLinear ? 1.0f : LLSETTINGS.pointLightMult) : 1.0f;
    float3 finalColor = isLinear ? color : LightToLinear(color);
    return finalColor * mult;
#else
    return color;
#endif 
}

float3 DirLightToLinear(float3 color)
{
#if defined(SKYRIM)       
    const bool isLinear = LLSETTINGS.isDirLightLinear;
    
    float mult = LLON ? (isLinear ? 1.0f : K_PI * LLSETTINGS.directionalLightMult * LLSETTINGS.dirLightMult) : K_PI;
    float3 finalColor = isLinear ? color : LightToLinear(color * PBRLightingScaleRcp);
    return finalColor * mult;
#else
    return color * K_PI;
#endif 
}

float3 GlowToLinear(float3 color)
{
#if defined(SKYRIM)
    return LLON ? pow(abs(color), LLSETTINGS.glowmapGamma) * LLSETTINGS.glowmapMult : color;
#else
    return color;
#endif
}

float VanillaDiffuseColorMult()
{
#if defined(SKYRIM)    
    return LLON ? LLSETTINGS.vanillaDiffuseColorMult : 1.0f;
#else
    return 1.0f;
#endif
}

float3 VanillaDiffuseColor(float3 color)
{
    return saturate(ColorToLinear(color) * VanillaDiffuseColorMult());
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
#if defined(SKYRIM)    
    return LLON ? color : pow(abs(color), 2.2f);
#else
    return color;
#endif    
}

float3 LLTrueLinearToGamma(float3 color)
{
#if defined(SKYRIM)     
    return LLON ? color : pow(abs(color), 1.0f / 2.2f);
#else
    return color;
#endif    
}

float3 EmitColorToLinear(float3 color)
{
#if defined(SKYRIM)      
    return pow(abs(color), LLON ? LLSETTINGS.emitColorGamma : 2.2f);
#else
    return color;
#endif  
}

float EmitColorMult()
{
#if defined(SKYRIM)       
    return LLON ? LLSETTINGS.emitColorMult : 1.0f;
#else
    return 1.0f;
#endif  
}
#endif