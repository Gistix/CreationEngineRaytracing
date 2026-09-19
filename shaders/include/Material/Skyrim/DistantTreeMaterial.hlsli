#ifndef DISTANT_TREE_MATERIAL_FUNC_HLSL
#define DISTANT_TREE_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/Surface.hlsli"
#include "interop/Properties.hlsli"
#include "include/ColorConversions.hlsli"

#include "interop/Material/Skyrim/DistantTreeMaterialData.hlsli"

void DistantTreeMaterial(inout Surface surface, in float2 texCoord0, in Mesh mesh, Properties props)
{
    DistantTreeMaterialData material = Materials[0].Load<DistantTreeMaterialData>(mesh.GetMaterialOffset());
    Texture2D treeLODAtlasTexture = Textures[NonUniformResourceIndex(material.TreeLODAtlas)];
    float4 diffuse = treeLODAtlasTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);
    float alpha = diffuse.a * props.Alpha;

    surface.Albedo = VanillaDiffuseColor(diffuse.rgb);
}

#endif // DISTANT_TREE_MATERIAL_FUNC_HLSL
