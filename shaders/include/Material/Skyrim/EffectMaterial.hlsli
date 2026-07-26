#ifndef EFFECT_MATERIAL_FUNC_HLSL
#define EFFECT_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Surface.hlsli"
#include "interop/Properties.hlsli"
#include "interop/Material/Skyrim/EffectMaterialData.hlsli"

#include "include/Material/Skyrim/Common.hlsli"

void EffectMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, in Mesh mesh, Properties props)
{
    EffectMaterialData effect = Materials[0].Load<EffectMaterialData>(mesh.GetMaterialOffset());
    const float mipLevel = surface.MipLevel;
    
    Texture2D baseTexture = Textures[NonUniformResourceIndex(effect.SourceTexture)];

    float4 baseTexColor = baseTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
    baseTexColor.xyz = baseTexColor.xyz;
    
    float4 baseColorMul = effect.BaseColor;
    baseColorMul.xyz = baseColorMul.xyz;
    
    [branch]
    if ((props.ShaderFlags & ShaderFlags::kVertexColors) && !(props.ShaderFlags & ShaderFlags::kProjectedUV))
    {
        baseColorMul *= float4(vertexColor.xyz, vertexColor.w);
    }

    float4 baseColor = float4(1, 1, 1, 1);
    float baseColorScale = effect.BaseColorScale;

    [branch]
    if (props.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
    {
        Texture2D effectTexture = Textures[NonUniformResourceIndex(effect.EffectTexture)];

        float2 grayscaleToColorUv = float2(baseTexColor.y, baseColorMul.x);

        baseColor.xyz = baseColorScale * effectTexture.SampleLevel(ClampSampler, grayscaleToColorUv, mipLevel).xyz;
    }
    else
    {
        baseColor = baseTexColor * baseColorMul;
    }

    surface.Albedo = 0;
    surface.Emissive = EffectToLinear(baseColor.xyz) * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Effect);
}

#endif // EFFECT_MATERIAL_FUNC_HLSL
