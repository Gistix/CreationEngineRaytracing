#ifndef SURFACEMAKER_HLSL
#define SURFACEMAKER_HLSL

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"

#if !defined(RASTER)
#   include "raytracing/include/Payload.hlsli"
#   include "raytracing/include/Geometry.hlsli"

#   include "raytracing/include/Materials/TexLODHelpers.hlsli"
#endif

#include "include/PBR.hlsli"

#include "include/Surface.hlsli"

#if !defined(GAME_DEF)
#define SKYRIM
#endif

#if defined(SKYRIM)   
#   include "include/SurfaceSkyrim.hlsli"
#elif defined(FALLOUT4)
#   include "include/SurfaceFallout4.hlsli"
#endif

struct SurfaceMaker
{

#if !defined(RASTER)
    static Surface make(float3 position, Payload payload, float3 rayDir, RayCone rayCone, out Instance instance, out Material material)
    { 
        Surface surface;         

        surface.Position = position;
        surface.SubsurfaceData = (Subsurface)0;
        surface.DiffTrans = 0.0f;
        surface.SpecTrans = 0.0f;

        Mesh mesh = GetMesh(payload, instance);

        // Loads all geometry releated data
        Vertex v0, v1, v2;
        GetVertices(mesh.GeometryIdx, payload.primitiveIndex, v0, v1, v2);
        float3 uvw = GetBary(payload.Barycentrics());

        material = mesh.Material;

        float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

        float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) mesh.Transform);

        float coneTexLODValue = ComputeRayConeTriangleLODValue(v0, v1, v2, objectToWorld3x3);

        float3 objectSpaceFlatNormal = SafeNormalize(cross(
            v1.Position - v0.Position,
            v2.Position - v0.Position));

        float3 normal0 = FlipIfOpposite(v0.Normal, objectSpaceFlatNormal);
        float3 normal1 = FlipIfOpposite(v1.Normal, objectSpaceFlatNormal);
        float3 normal2 = FlipIfOpposite(v2.Normal, objectSpaceFlatNormal);

        float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(normal0, normal1, normal2, uvw)));
        float3 bitangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));
        float3 tangentWS = cross(normalWS, bitangentWS) * Interpolate(v0.Handedness, v1.Handedness, v2.Handedness, uvw);        
        
        surface.FaceNormal = mul(objectToWorld3x3, objectSpaceFlatNormal);

        surface.MipLevel = rayCone.computeLOD(coneTexLODValue, rayDir, normalWS, true) + Raytracing.TexLODBias;
        Texture2D baseTextureForLod = Textures[NonUniformResourceIndex(material.BaseTexture())];
        uint baseTexWidth, baseTexHeight;
        baseTextureForLod.GetDimensions(baseTexWidth, baseTexHeight);
        surface.MipLevel += 0.5f * SafeLog2(max(1.0f, (float)baseTexWidth * (float)baseTexHeight));
        
        surface.GeomNormal = normalWS;
        surface.GeomTangent = tangentWS;

        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
        surface.Emissive = float3(0.0f, 0.0f, 0.0f);
        surface.TransmissionColor = float3(0.0f, 0.0f, 0.0f);
        surface.Roughness = PBR::Defaults::Roughness;
        surface.Metallic = PBR::Defaults::Metallic;
        surface.AO = 1.0f;
        surface.F0 = PBR::Defaults::F0;
    
#       if defined(SKYRIM)
        if (material.Feature == Feature::kMultiTexLandLODBlend)
        {
            LandMaterial(surface, v0, v1, v2, uvw, normalWS, tangentWS, bitangentWS, material);
        }
        else
        {
            DefaultMaterial(surface, v0, v1, v2, uvw, normalWS, tangentWS, bitangentWS, objectToWorld3x3, material);
        }
#       else   
#       endif
   
        return surface; 
    }  
#endif
    
    static Surface make(float3 position, float3 geomNormal, float3 normal, float3 tangent, float3 bitangent, float3 albedo, float roughness, float metallic, float3 emissive, float ao) 
    { 
        Surface surface;         

        surface.SubsurfaceData = (Subsurface)0;
        surface.DiffTrans = 0.0f;
        surface.SpecTrans = 0.0f;

        surface.Position = position;

        surface.FaceNormal = geomNormal;

        surface.MipLevel = 0.0f + Raytracing.TexLODBias;
        surface.GeomNormal = geomNormal;
        surface.GeomTangent = tangent; // not needed for hybrid

        surface.Normal = normal;
        surface.Tangent = tangent;
        surface.Bitangent = bitangent;

#   ifdef DEBUG_WHITE_FURNACE
        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
#   else
        surface.Albedo = albedo;
#   endif
        surface.TransmissionColor = float3(0.0f, 0.0f, 0.0f);
        surface.Emissive = emissive * Raytracing.Emissive;
        
        surface.Roughness = PBR::Roughness(roughness, Raytracing.Roughness.x, Raytracing.Roughness.y);
        surface.Metallic = Remap(metallic, Raytracing.Metalness.x, Raytracing.Metalness.y);
        surface.AO = ao;
        
        surface.DiffuseAlbedo = surface.Albedo * (1.0f - surface.Metallic);

        surface.F0 = PBR::F0(albedo, metallic);
        surface.IOR = F0toIOR(surface.F0);

#if defined(FULL_MATERIAL)
        surface.SubsurfaceColor = float3(0.0f, 0.0f, 0.0f);
        surface.Thickness = 0.0f;
        surface.CoatColor = float3(1.0f, 1.0f, 1.0f);
        surface.CoatStrength = 0.0f;
        surface.CoatRoughness = 0.0f;
        surface.CoatF0 = float3(0.04f, 0.04f, 0.04f);
        surface.FuzzColor = float3(0.0f, 0.0f, 0.0f);
        surface.FuzzWeight = 0.0f;
        surface.GlintScreenSpaceScale = 1.0f;
        surface.GlintLogMicrofacetDensity = 0.0f;
        surface.GlintMicrofacetRoughness = 0.0f;
        surface.GlintDensityRandomization = 0.0f;
        surface.Noise = 0.0f;
#endif         
        
        return surface; 
    }
};

#endif // SURFACEMAKER_HLSL