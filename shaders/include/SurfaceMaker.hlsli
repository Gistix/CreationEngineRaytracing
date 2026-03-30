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
#   define SKYRIM
#endif

#if defined(SKYRIM)   
#   include "include/SurfaceSkyrim.hlsli"
#elif defined(FALLOUT4)
#   include "include/SurfaceFallout4.hlsli"
#endif

struct SurfaceMaker
{

#if !defined(RASTER)
    static Surface make(float3 position, Payload payload, float3 rayDir, RayCone rayCone, out Instance instance, out Material material, bool primary)
    { 
        Surface surface;         

        surface.Primary = primary;
        
        surface.Position = position;
        surface.PrevPosition = position;
        surface.SubsurfaceData = (Subsurface)0;
        surface.DiffTrans = 0.0f;
        surface.SpecTrans = 0.0f;
        surface.IsThinSurface = false;

        Mesh mesh = GetMesh(payload, instance);

        // Loads all geometry releated data
        Vertex v0, v1, v2;
        GetVertices(mesh.GeometryIdx, payload.primitiveIndex, v0, v1, v2);
        float3 uvw = GetBary(payload.Barycentrics());

        material = mesh.Material;

        float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
       
        float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) mesh.Transform);

        // Compute previous world position for motion vectors
        {
#if defined(HAS_PREV_POSITIONS)
            if (mesh.Flags & MeshDataFlags::Skinned)
            {
                // Per-vertex: read previous skinned positions from PrevPositions buffer
                Triangle tri = GetTriangle(mesh.GeometryIdx, payload.primitiveIndex);
                float3 prevPos0 = PrevPositions[NonUniformResourceIndex(mesh.GeometryIdx)][NonUniformResourceIndex(tri.x)];
                float3 prevPos1 = PrevPositions[NonUniformResourceIndex(mesh.GeometryIdx)][NonUniformResourceIndex(tri.y)];
                float3 prevPos2 = PrevPositions[NonUniformResourceIndex(mesh.GeometryIdx)][NonUniformResourceIndex(tri.z)];
                float3 prevRootPos = Interpolate(prevPos0, prevPos1, prevPos2, uvw);
                surface.PrevPosition = mul(instance.PrevTransform, float4(prevRootPos, 1.0));
            }
            else
#endif
            {
                // Per-object: use current vertex positions with previous instance transform
                float3 objectSpacePos = Interpolate(v0.Position, v1.Position, v2.Position, uvw);
                float3 rootSpacePos = mul(mesh.Transform, float4(objectSpacePos, 1.0));
                surface.PrevPosition = mul(instance.PrevTransform, float4(rootSpacePos, 1.0));
            }
        }

        float coneTexLODValue = ComputeRayConeTriangleLODValue(v0, v1, v2, objectToWorld3x3);

        float3 objectSpaceFlatNormal = SafeNormalize(cross(
            v1.Position - v0.Position,
            v2.Position - v0.Position));

        float3 normal0 = FlipIfOpposite(v0.Normal, objectSpaceFlatNormal);
        float3 normal1 = FlipIfOpposite(v1.Normal, objectSpaceFlatNormal);
        float3 normal2 = FlipIfOpposite(v2.Normal, objectSpaceFlatNormal);

        float handedness = Interpolate(v0.Handedness, v1.Handedness, v2.Handedness, uvw);
        
        float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(normal0, normal1, normal2, uvw)));
        float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
        float3 bitangentWS = cross(tangentWS, normalWS) * handedness;        
        
        float4 vertexColor = Interpolate(v0.Color.unpack(), v1.Color.unpack(), v2.Color.unpack(), uvw);

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
        surface.VolumeAbsorption = float3(0.0f, 0.0f, 0.0f);
        surface.Roughness = PBR::Defaults::Roughness;
        surface.Metallic = PBR::Defaults::Metallic;
        
        surface.AO = 1.0f;
        surface.F0 = PBR::Defaults::F0;

        surface.CoatColor = float3(1.0f, 1.0f, 1.0f);
        surface.CoatStrength = 0.0f;
        surface.CoatRoughness = 0.0f;
        surface.CoatF0 = float3(0.04f, 0.04f, 0.04f);
        surface.CoatNormal = normalWS;
        surface.CoatTangent = tangentWS;
        surface.CoatBitangent = bitangentWS;
    
#   if defined(SKYRIM)
        if (material.Feature == Feature::kMultiTexLandLODBlend)
        {
            float4 landBlend0 = Interpolate(v0.LandBlend0.unpack(), v1.LandBlend0.unpack(), v2.LandBlend0.unpack(), uvw);
            float4 landBlend1 = Interpolate(v0.LandBlend1.unpack(), v1.LandBlend1.unpack(), v2.LandBlend1.unpack(), uvw);
            
            LandMaterial(surface, texCoord0, vertexColor, normalWS, tangentWS, bitangentWS, handedness, landBlend0, landBlend1, material);
        }
        else if (material.ShaderType == ShaderType::Water)
        {
            WaterMaterial(surface, texCoord0, tangentWS, bitangentWS, handedness, material);
        }
        else
        {
            DefaultMaterial(surface, texCoord0, vertexColor, normalWS, tangentWS, bitangentWS, handedness, material);
        }
#   else   
#   endif
   
        surface.Roughness = PBR::Roughness(surface.Roughness, Raytracing.Roughness.x, Raytracing.Roughness.y);
        surface.Metallic = Remap(surface.Metallic, Raytracing.Metalness.x, Raytracing.Metalness.y);

        surface.DiffuseAlbedo = surface.Albedo * (1.0f - surface.Metallic);

        surface.F0 = PBR::F0(surface.F0, surface.Albedo, surface.Metallic);
        surface.IOR = F0toIOR(surface.F0);
               
        return surface; 
    }  
#endif
  
    static Surface make(float3 position, float2 texCoord, float3 normalWS, float3 tangentWS, float3 bitangentWS, float4 vertexColor, float4 landBlend0, float4 landBlend1, Mesh mesh)
    { 
        Surface surface;         

        surface.Primary = false;
        
        surface.Position = position;
        surface.PrevPosition = position;
        surface.SubsurfaceData = (Subsurface)0;
        surface.DiffTrans = 0.0f;
        surface.SpecTrans = 0.0f;
        surface.IsThinSurface = false;

        Material material = mesh.Material;

        float2 texCoord0 = material.TexCoord(texCoord);

        surface.FaceNormal = normalWS;

        surface.MipLevel = 0;

        surface.GeomNormal = normalWS;
        surface.GeomTangent = tangentWS;

        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
        surface.Emissive = float3(0.0f, 0.0f, 0.0f);
        surface.TransmissionColor = float3(0.0f, 0.0f, 0.0f);
        surface.VolumeAbsorption = float3(0.0f, 0.0f, 0.0f);
        surface.Roughness = PBR::Defaults::Roughness;
        surface.Metallic = PBR::Defaults::Metallic;
        
        surface.AO = 1.0f;
        surface.F0 = PBR::Defaults::F0;

        surface.CoatColor = float3(1.0f, 1.0f, 1.0f);
        surface.CoatStrength = 0.0f;
        surface.CoatRoughness = 0.0f;
        surface.CoatF0 = float3(0.04f, 0.04f, 0.04f);
        surface.CoatNormal = normalWS;
        surface.CoatTangent = tangentWS;
        surface.CoatBitangent = bitangentWS;
    
        float handedness = (dot(cross(normalWS, tangentWS), bitangentWS) < 0.0f) ? -1.0f : 1.0f;
        
#   if defined(SKYRIM)
        if (material.Feature == Feature::kMultiTexLandLODBlend)
            LandMaterial(surface, texCoord0, vertexColor, normalWS, tangentWS, bitangentWS, handedness, landBlend0, landBlend1, material);
        else
            DefaultMaterial(surface, texCoord0, vertexColor, normalWS, tangentWS, bitangentWS, handedness, material);
#   else   
#   endif
   
        surface.Roughness = PBR::Roughness(surface.Roughness, Raytracing.Roughness.x, Raytracing.Roughness.y);
        surface.Metallic = Remap(surface.Metallic, Raytracing.Metalness.x, Raytracing.Metalness.y);

        surface.DiffuseAlbedo = surface.Albedo * (1.0f - surface.Metallic);

        surface.F0 = PBR::F0(surface.F0, surface.Albedo, surface.Metallic);
        surface.IOR = F0toIOR(surface.F0);
        
        
        return surface; 
    }    
    
    static Surface make(float3 position, float3 geomNormal, float3 normal, float3 tangent, float3 bitangent, float3 albedo, float roughness, float metallic, float3 emissive, float ao) 
    { 
        Surface surface;         

        surface.Primary = false;        
        
        surface.SubsurfaceData = (Subsurface)0;
        surface.DiffTrans = 0.0f;
        surface.SpecTrans = 0.0f;
        surface.IsThinSurface = false;

        surface.Position = position;
        surface.PrevPosition = position;

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
        surface.VolumeAbsorption = float3(0.0f, 0.0f, 0.0f);
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
        surface.CoatNormal = normal;
        surface.CoatTangent = tangent;
        surface.CoatBitangent = bitangent;
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