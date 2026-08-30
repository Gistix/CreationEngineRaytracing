#ifndef EFFECT_MATERIAL_FUNC_HLSL
#define EFFECT_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Surface.hlsli"
#include "interop/Properties.hlsli"
#include "interop/Material/Fallout4/EffectMaterialData.hlsli"

#include "include/Material/Common.hlsli"
#include "include/Material/Fallout4/Common.hlsli"

void EffectMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, in Mesh mesh, Properties props)
{
    EffectMaterialData material = Materials[0].Load<EffectMaterialData>(mesh.GetMaterialOffset());
    const float mipLevel = surface.MipLevel;
    
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.SourceTexture)];
    float4 baseTexColor = baseTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);

    float4 baseColor = material.BaseColor * material.BaseColorScale;
    
    [branch]
    if (props.ShaderFlags & ShaderFlags::kEnvMap)
    {
        surface.Albedo = float3(1.0f, 1.0f, 1.0f);
        surface.Roughness = 0.0f;
        surface.F0 = 0.04f;
        surface.TransmissionColor = 1.0f;
        surface.SpecTrans = 1.0f;
        surface.IsThinSurface = true;
  
        MaterialAlpha(props, baseTexColor.r, baseColor.a, vertexColor.a, surface);
    }
    else
    {
        float4 baseColorMul = material.BaseColor;

        [branch]
        if ((props.ShaderFlags & ShaderFlags::kVertexColors) && !(props.ShaderFlags & ShaderFlags::kProjectedUV))
        {
            baseColorMul *= float4(vertexColor.xyz, vertexColor.w);
        }

        float4 baseColor = float4(1, 1, 1, 1);
        float baseColorScale = material.BaseColorScale;

        [branch]
        if (props.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
        {
            Texture2D effectTexture = Textures[NonUniformResourceIndex(material.EffectTexture)];

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
}

#endif // EFFECT_MATERIAL_FUNC_HLSL
