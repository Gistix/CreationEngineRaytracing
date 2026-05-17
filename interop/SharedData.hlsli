// A lighter version of SharedData containing only the necessary structs for HLSL 6.0+ compatibility

#ifndef SHARED_DATA_HLSL
#define SHARED_DATA_HLSL

#include "Interop.h"

#ifndef __cplusplus
typedef bool BOOL;
#endif

struct CPMSettings
{
    BOOL EnableComplexMaterial;
    BOOL EnableParallax;
    BOOL EnableTerrainParallax;
    BOOL EnableHeightBlending;
    BOOL EnableShadows;
    BOOL ExtendShadows;
    BOOL EnableParallaxWarpingFix;
    BOOL pad0;
};
#ifdef __cplusplus
static_assert(sizeof(CPMSettings) % 16 == 0);
#endif

struct WetnessEffectsSettings
{
    #ifndef __cplusplus
    row_major
    #endif
    float4x4 OcclusionViewProj;

    float Time;
    float Raining;
    float Wetness;
    float PuddleWetness;

    BOOL EnableWetnessEffects;
    float MaxRainWetness;
    float MaxPuddleWetness;
    float MaxShoreWetness;

    uint ShoreRange;
    float PuddleRadius;
    float PuddleMaxAngle;
    float PuddleMinWetness;

    float MinRainWetness;
    float SkinWetness;
    float WeatherTransitionSpeed;
    BOOL EnableRaindropFx;

    BOOL EnableSplashes;
    BOOL EnableRipples;
    uint EnableVanillaRipples;
    float RaindropFxRange;

    float RaindropGridSizeRcp;
    float RaindropIntervalRcp;
    float RaindropChance;
    float SplashesLifetime;

    float SplashesStrength;
    float SplashesMinRadius;
    float SplashesMaxRadius;
    float RippleStrength;

    float RippleRadius;
    float RippleBreadth;
    float RippleLifetimeRcp;
    float pad0;
};
#ifdef __cplusplus
static_assert(sizeof(WetnessEffectsSettings) % 16 == 0);
#endif

struct CloudShadowsSettings
{
    float Opacity;
    float3 pad0;
};
#ifdef __cplusplus
static_assert(sizeof(CloudShadowsSettings) % 16 == 0);
#endif

struct HairSpecularSettings
{
    uint Enabled;
    float HairGlossiness;
    float SpecularMult;
    float DiffuseMult;
    uint EnableTangentShift;
    float PrimaryTangentShift;
    float SecondaryTangentShift;
    float HairSaturation;
    float SpecularIndirectMult;
    float DiffuseIndirectMult;
    float BaseColorMult;
    float Transmission;
    uint EnableSelfShadow;
    float SelfShadowStrength;
    float SelfShadowExponent;
    float SelfShadowScale;
    uint HairMode; // 0: Kajiya-Kay, 1: Marschner
    uint pad0;
    uint pad1;
    uint pad2;
};
#ifdef __cplusplus
static_assert(sizeof(HairSpecularSettings) % 16 == 0);
#endif

struct ExtendedTranslucencySettings
{
    uint MaterialModel; // [0,1,2,3] The MaterialModel
    float Reduction; // [0, 1.0] The factor to reduce the transparency to matain the average transparency [0,1]
    float Softness; // [0, 2.0] The soft remap upper limit [0,2]
    float Strength; // [0, 1.0] The inverse blend weight of the effect
};
#ifdef __cplusplus
static_assert(sizeof(ExtendedTranslucencySettings) % 16 == 0);
#endif

struct LinearLightingSettings
{
	uint enableLinearLighting;
	uint enableGammaCorrection;
	uint enableACEScg;
	uint isDirLightLinear;
	float dirLightMult;
	float lightGamma;
	float colorGamma;
	float emitColorGamma;
	float glowmapGamma;
	float ambientGamma;
	float fogGamma;
	float fogAlphaGamma;
	float effectGamma;
	float effectAlphaGamma;
	float skyGamma;
	float waterGamma;
	float vlGamma;
	float vanillaDiffuseColorMult;
	float directionalLightMult;  // Computed based on interior/exterior
	float pointLightMult;
	float ambientMult;
	float emitColorMult;
	float glowmapMult;
	float effectLightingMult;
	float membraneEffectMult;
	float bloodEffectMult;
	float projectedEffectMult;
	float deferredEffectMult;
	float otherEffectMult;
	float _pad0;
	float _pad1;
	float _pad2;
};
#ifdef __cplusplus
static_assert(sizeof(LinearLightingSettings) % 16 == 0);
#endif

struct ExponentialHeightFogSettings
{
    uint enabled;
    uint useDynamicCubemaps;
    float startDistance;
    float fogHeight;
    float fogHeightFalloff;
    float fogDensity;
    float directionalInscatteringMultiplier;
    float directionalInscatteringAnisotropy;
    float4 inscatteringTint;
    float cubemapMipLevel;
    float sunlightAttenuationAmount;
    uint respectVanillaFogFade;
    uint disableVanillaFog;
    float4 fogInscatteringColor;
    float originalFogColorAmount;
    float3 pad;
};
#ifdef __cplusplus
static_assert(sizeof(ExponentialHeightFogSettings) % 16 == 0);
#endif

struct PhysSkyData
{
    // DYNAMIC
    float2 texDim;
    float2 rcpTexDim;
    float2 frameDim;
    float2 rcpFrameDim;

    float zCameraPlanet;
    float3 sunDir;
    float3 sunlightColor;
    float trMix;
    float3 masserDir;
    float apLumMix;
    float3 masserColor;
    float apTrMix;
    float3 secundaDir;
    float sunDiskCos;
    float3 secundaColor;

    // GENERAL
    uint enabled;
    int tonemapper;
    float vanillaMix;

    // WORLD
    float zBottom;
    float rPlanet;
    float rAtmosphere;
    float3 groundAlbedo;

    // ATMOSPHERE
    float2 cloudShadowRemapRange;

    float aerosolFalloff;
    float aerosolPhaseG;
    float3 aerosolScatter;
    uint halfResApShadow;
    float3 aerosolAbsorption;

    float rayleighFalloff;
    float3 rayleighScatter;

    float ozoneAltitude;
    float ozoneThickness;
    float3 ozoneAbsorption;

    // CLOUDS (VANILLA)
    float cloudRelightMix;
    float cloudOriginalMix;
    float silverLiningMix;
    float silverLiningSpread;
};
#ifdef __cplusplus
static_assert(sizeof(PhysSkyData) % 16 == 0);
#endif

struct SkinData
{
    float4 skinParams;
    float4 skinParams2;
    float4 skinDetailParams;
    float4 sssParams;
    float4 fuzzParams;
    float4 physicalParams;
    float4 wetParams;
};
#ifdef __cplusplus
static_assert(sizeof(SkinData) % 16 == 0);
#endif

INTEROP_STRUCT(FeatureData, 16)
{
    CPMSettings ExtendedMaterial;
    WetnessEffectsSettings WetnessEffects;
    CloudShadowsSettings CloudShadows;
    HairSpecularSettings HairSpecular;
    ExtendedTranslucencySettings ExtendedTranslucency;
    LinearLightingSettings LinearLighting;
    ExponentialHeightFogSettings ExponentialHeightFog;
    PhysSkyData PhysicalSky;
    SkinData Skin;
};
VALIDATE_CBUFFER(FeatureData, 16);

#endif
