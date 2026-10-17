#ifndef GRASS_MATERIAL_FUNC_HLSL
#define GRASS_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Surface.hlsli"
#include "include/Utils/VanillaToPBR.hlsli"
#include "interop/Material/Fallout4/LightingMaterialData.hlsli"

void GrassMaterial(inout Surface surface, in float2 texCoord0, in Mesh mesh, Properties props)
{
    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.DiffuseTexture)];
    float4 diffuse = baseTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);
    float alpha = diffuse.a * props.Alpha;

    surface.Albedo = VanillaDiffuseColor(diffuse.rgb);

    // Foliage translucency: thin two-sided surface with diffuse transmission
    surface.IsThinSurface = true;
    surface.TransmissionColor = surface.Albedo * 0.4f;
    surface.DiffTrans = 0.4f;
    surface.Roughness = 0.6f;
}

#endif // GRASS_MATERIAL_FUNC_HLSL
