#ifndef LAND_MATERIAL_FUNC_HLSL
#define LAND_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Surface.hlsli"
#include "include/Utils/VanillaToPBR.hlsli"
#include "interop/Properties.hlsli"
#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Fallout4/LandscapeMaterialData.hlsli"

#include "include/Material/Fallout4/Common.hlsli"

void LandMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, float3 normalWS, float3 tangentWS, float3 bitangentWS, float4 landBlend0, float4 landBlend1, in Mesh mesh, float3 viewDir, float dist)
{
    LandscapeMaterialData material = Materials[0].Load<LandscapeMaterialData>(mesh.GetMaterialOffset());

    Texture2D diffuseTexture = Textures[NonUniformResourceIndex(material.DiffuseTexture1)];
    float4 diffuse = diffuseTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);

    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture1)];
    float4 normalMap = normalTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);

    surface.Albedo = ColorToLinear(diffuse.xyz) * ColorToLinear(vertexColor.xyz);
    
    surface.GeomNormal = normalWS;

    float3 normal = normalMap.xyz * 2.0 - 1.0;
    float3x3 tbnTr = float3x3(tangentWS, bitangentWS, normalWS);
    surface.Normal = normalize(mul(normal, tbnTr));

    surface.Roughness = 0.8f; 
    surface.Metallic = 0.0f;
    
    surface.F0 = float3(0.04f, 0.04f, 0.04f);
}

#endif // LAND_MATERIAL_FUNC_HLSL


