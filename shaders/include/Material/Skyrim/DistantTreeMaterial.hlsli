#ifndef DISTANT_TREE_MATERIAL_FUNC_HLSL
#define DISTANT_TREE_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/Surface.hlsli"
#include "interop/Properties.hlsli"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

void DistantTreeMaterial(inout Surface surface, in float2 texCoord0, in Mesh mesh, Properties props)
{
    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.DiffuseTexture)];
    float4 diffuse = baseTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);
    float alpha = diffuse.a * props.Alpha;

    surface.Albedo = diffuse.rgb;
}

#endif // DISTANT_TREE_MATERIAL_FUNC_HLSL
