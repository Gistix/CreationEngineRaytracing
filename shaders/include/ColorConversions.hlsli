#ifndef COLOR_CONVERSIONS_COMMON_HLSLI
#define COLOR_CONVERSIONS_COMMON_HLSLI

#include "interop/SharedData.hlsli"
#include "Utils/MathConstants.hlsli"
#include "include/Common/ColorSpaces.hlsli"
#include "include/Common/TransferFunctions.hlsli"

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

float3 LinearSRGBToWorking(float3 color)
{
#if defined(SKYRIM)
    return LLON && LLSETTINGS.enableACEScg ? sRGBToAP1(color) : color;
#else
    return color;
#endif
}

float3 WorkingToLinearSRGB(float3 color)
{
#if defined(SKYRIM)
    return LLON && LLSETTINGS.enableACEScg ? AP1TosRGB(color) : color;
#else
    return color;
#endif
}

float3 SRGBColorToLinear(float3 color)
{
#if defined(SKYRIM)
    return LinearSRGBToWorking(TransferFunctions::SRGBToLinear(color));
#else
    return color;
#endif
}

float3 PBRColorScale(float3 color)
{
#if defined (SKYRIM)   
    return LinearSRGBToWorking(color) * PBRLightingScale;
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
    return TransferFunctions::LinearToGameGamma(WorkingToLinearSRGB(color));
#else
    return color;
#endif 
}

float3 ColorToLinear(float3 color)
{
#if defined(SKYRIM)    
    return LinearSRGBToWorking(TransferFunctions::GameGammaToLinear(color));
#else
    return color;
#endif    
}

float3 EffectToLinear(float3 color)
{
#if defined(SKYRIM)
    return ColorToLinear(color) * (LLON ? LLSETTINGS.effectLightingMult : 1.0f);
#else
    return color;
#endif 
}

float3 LightToLinear(float3 color)
{
#if defined(SKYRIM)
    return SRGBColorToLinear(color);
#else
    return color;
#endif 
}

float3 PointLightToLinear(float3 color, bool isLinear)
{
#if defined(SKYRIM)    
    float mult = LLON ? LLSETTINGS.pointLightMult : 1.0f;
    float3 finalColor = isLinear ? LinearSRGBToWorking(color) : LightToLinear(color);
    return finalColor * mult;
#else
    return color;
#endif 
}

float3 DirLightToLinear(float3 color)
{
#if defined(SKYRIM)       
    float4 light = LLSETTINGS.directionalLightColor;
    float3 finalColor = LLON ? light.rgb : LightToLinear(light.rgb * PBRLightingScaleRcp);
    return finalColor * light.a * (LLON ? LLSETTINGS.directionalLightMult : K_PI);
#else
    return color * K_PI;
#endif 
}

float3 GlowToLinear(float3 color)
{
#if defined(SKYRIM)
    return ColorToLinear(color) * (LLON ? LLSETTINGS.glowmapMult : 1.0f);
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
    return ColorToGamma(color / max(VanillaDiffuseColorMult(), 1e-5f));
}

float3 LLGammaToTrueLinear(float3 color)
{
#if defined(SKYRIM)    
    return LLON ? color : TransferFunctions::GameGammaToLinear(color);
#else
    return color;
#endif    
}

float3 LLTrueLinearToGamma(float3 color)
{
#if defined(SKYRIM)     
    return LLON ? color : TransferFunctions::LinearToGameGamma(color);
#else
    return color;
#endif    
}

float3 EmitColorToLinear(float3 color)
{
#if defined(SKYRIM)      
    return SRGBColorToLinear(color);
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