#ifndef SURFACE_HLSL
#define SURFACE_HLSL

#include "include/Common.hlsli"
#include "include/PBR.hlsli"
#include "raytracing/include/AdvancedSettings.hlsli"

struct Subsurface
{
    half3 TransmissionColor;
    half Scale;
    half3 ScatteringColor;
    half Anisotropy;
    uint HasSubsurface;
};

struct SurfaceGeometry
{
    float3 Position;
    float3 CameraRelativePosition;
    float3 PrevCameraRelativePosition;
    half3 GeomNormal;
    half3 Normal;
    half3 FaceNormal;
    half MipLevel;
    float PositionError;
#if USE_SIA_INTERPOLATION
    half SIAOffset; // NVIDIA SIA safe spawn offset distance
#endif

    void FlipNormal()
    {
        Normal = -Normal;
        GeomNormal = -GeomNormal;
        FaceNormal = -FaceNormal;
    }
};

struct SurfaceFrame
{
    half3 Tangent;
    half3 Bitangent;
    half3 GeomTangent;

    float3 Mul(half3 normal, float3 tangentSample)
    {
        return (float3)Tangent * tangentSample.x +
               (float3)Bitangent * tangentSample.y +
               (float3)normal * tangentSample.z;
    }

    float3 ToLocal(half3 normal, float3 v)
    {
        return float3(
            dot(v, (float3)Tangent),
            dot(v, (float3)Bitangent),
            dot(v, (float3)normal)
        );
    }

    float3 FromLocal(half3 normal, float3 v)
    {
        return Mul(normal, v);
    }
};

struct SurfaceMaterial
{
    half3 Albedo;
    half Alpha;
    half3 DiffuseAlbedo;
    half Roughness;
    half Metallic;
    half3 Emissive;
    half AO;
    half3 F0;
    half IOR;
    half3 TransmissionColor;
    half3 VolumeAbsorption;
    half DiffTrans;
    half SpecTrans;
    bool IsThinSurface;
};

struct SurfaceCoat
{
    half3 Color;
    half Strength;
    half Roughness;
    half3 F0;
    half3 Normal;
    half3 Tangent;
    half3 Bitangent;

    bool HasCoat()
    {
        return Strength > 0.0f;
    }

    float3 ToLocal(float3 v)
    {
        return float3(
            dot(v, (float3)Tangent),
            dot(v, (float3)Bitangent),
            dot(v, (float3)Normal)
        );
    }

    float3 FromLocal(float3 v)
    {
        return (float3)Tangent * v.x +
               (float3)Bitangent * v.y +
               (float3)Normal * v.z;
    }
};

struct SurfaceFuzz
{
    half3 Color;
    half Weight;

    bool HasFuzz()
    {
        return Weight > 0.0f;
    }
};

#if defined(GLINT)
struct SurfaceGlint
{
    half ScreenSpaceScale;
    half LogMicrofacetDensity;
    half MicrofacetRoughness;
    half DensityRandomization;
    float2 TexCoord;
};
#endif

struct Surface
{
    bool Primary;
    
    SurfaceGeometry Geometry;
    SurfaceFrame Frame;
    SurfaceMaterial Material;
    Subsurface SubsurfaceData;
    SurfaceCoat Coat;
    SurfaceFuzz Fuzz;
#if defined(GLINT)
    SurfaceGlint Glint;
#endif

    float3 Mul(float3 tangentSample)
    {
        return Frame.Mul(Geometry.Normal, tangentSample);
    }

    float3 ToLocal(float3 v)
    {
        return Frame.ToLocal(Geometry.Normal, v);
    }

    float3 FromLocal(float3 v)
    {
        return Frame.FromLocal(Geometry.Normal, v);
    }

    float3 CoatToLocal(float3 v)
    {
        return Coat.ToLocal(v);
    }

    float3 CoatFromLocal(float3 v)
    {
        return Coat.FromLocal(v);
    }

    void FlipNormal()
    {
        Geometry.FlipNormal();
    }
};

struct BRDFContext {
    float3 ViewDirection;
    half NdotV;

    void __init(Surface surface, float3 viewDirection)
    {
        ViewDirection = viewDirection;
        NdotV = (half)saturate(dot((float3)surface.Geometry.Normal, viewDirection));
    }   

    void __init(SurfaceGeometry geom, float3 viewDirection)
    {
        ViewDirection = viewDirection;
        NdotV = (half)saturate(dot((float3)geom.Normal, viewDirection));
    }
    
    static BRDFContext make(Surface surface, float3 viewDirection) 
    { 
        BRDFContext ret;         
        ret.__init(surface, viewDirection); 
        return ret; 
    }   

    static BRDFContext make(SurfaceGeometry geom, float3 viewDirection) 
    { 
        BRDFContext ret;         
        ret.__init(geom, viewDirection); 
        return ret; 
    }   
};

#endif // SURFACE_HLSL
