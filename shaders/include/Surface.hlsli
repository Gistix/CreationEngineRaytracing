#ifndef SURFACE_HLSL
#define SURFACE_HLSL

#include "include/Common.hlsli"
#include "include/PBR.hlsli"

struct Subsurface
{
    float3 TransmissionColor;
    float Scale;
    float3 ScatteringColor;
    float Anisotropy;
    uint HasSubsurface;
};

struct Surface
{
    bool Primary;
    
    float3 Position;
    float3 GeomNormal;
    float3 GeomTangent;
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
    float3 FaceNormal;
    float3 Albedo;
    float Alpha;
    float3 DiffuseAlbedo;
    float Roughness;
    float Metallic;
    float3 Emissive;
    float AO;
    float3 F0;
    float IOR;
    float3 TransmissionColor;
    Subsurface SubsurfaceData;
    float DiffTrans;
    float SpecTrans;
    bool IsThinSurface;

#if defined(FULL_MATERIAL)
    float3 SubsurfaceColor;
    float Thickness;
    float3 CoatColor;
    float CoatStrength;
    float CoatRoughness;
    float3 CoatF0;
    float3 FuzzColor;
    float FuzzWeight;
    float GlintScreenSpaceScale;
    float GlintLogMicrofacetDensity;
    float GlintMicrofacetRoughness;
    float GlintDensityRandomization;
    //Glints::GlintCachedVars GlintCache;
    float Noise;
#endif

    float MipLevel;

    float3 Mul(float3 tangentSample)
    {
        return Tangent * tangentSample.x +
               Bitangent * tangentSample.y +
               Normal * tangentSample.z;
    }

    float3 ToLocal(float3 v)
    {
        return float3(
            dot(v, Tangent),
            dot(v, Bitangent),
            dot(v, Normal)
        );
    }

    float3 FromLocal(float3 v)
    {
        return Mul(v);
    }

    void FlipNormal()
    {
        Normal = -Normal;
        GeomNormal = -GeomNormal;
        FaceNormal = -FaceNormal;
    }
    
};

struct BRDFContext {
    float3 ViewDirection;
    float NdotV;

    void __init(Surface surface, float3 viewDirection)
    {
        ViewDirection = viewDirection;
        NdotV = saturate(dot(surface.Normal, viewDirection));
    }   
    
    static BRDFContext make(Surface surface, float3 viewDirection) 
    { 
        BRDFContext ret;         
        ret.__init(surface, viewDirection); 
        return ret; 
    }   
};

#endif // SURFACE_HLSL