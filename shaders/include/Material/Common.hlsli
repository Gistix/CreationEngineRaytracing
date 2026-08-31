#ifndef MATERIAL_COMMON_HLSL
#define MATERIAL_COMMON_HLSL

#include "include/Common.hlsli"

float4 BlendLandTexture(uint16_t textureIndex, float2 texcoord, float weight, float mipLevel)
{
    if (weight > LAND_MIN_WEIGHT)
    {
        Texture2D texture = Textures[NonUniformResourceIndex(textureIndex)];
        return texture.SampleLevel(DefaultSampler, texcoord, mipLevel) * weight;
    }
    else
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
}

void MaterialAlpha(Properties props, float alpha, float materialAlpha, float vertexAlpha, inout Surface surface)
{
    [branch]
    if (props.AlphaFlags != AlphaFlags::None)
    {
        alpha *= materialAlpha * props.Alpha;
        
        [branch]
        if ((props.ShaderFlags & ShaderFlags::kVertexAlpha) && !(props.ShaderFlags & ShaderFlags::kTreeAnim))
            alpha *= vertexAlpha;

        [branch]
        if (props.AlphaFlags & AlphaFlags::Transmission)
        {
            surface.Material.TransmissionColor = lerp(float3(1.0f, 1.0f, 1.0f), surface.Material.Albedo, alpha);
            surface.Material.Albedo *= alpha;
            surface.Material.Metallic *= alpha;
            surface.Material.SpecTrans = 1.0f;
            surface.Material.IsThinSurface |= (props.ShaderFlags & ShaderFlags::kTwoSided) != 0;
        }

        [branch]
        if (props.AlphaFlags & AlphaFlags::Additive)
        {
            surface.Material.Albedo = 0.0f;
            surface.Material.Metallic = 0.0f;
            surface.Material.Roughness = 0.0f;
            surface.Material.TransmissionColor = 1.0f;
            surface.Material.SpecTrans = 1.0f;
            surface.Material.F0 = 0.04f;

            surface.SubsurfaceData.HasSubsurface = 0;
            surface.SubsurfaceData.TransmissionColor = 0.0f;
            surface.SubsurfaceData.ScatteringColor = 0.0f;
            surface.SubsurfaceData.Scale = 0.0f;
            surface.SubsurfaceData.Anisotropy = 0.0f;

            surface.Coat.Color = 1.0f;
            surface.Coat.Strength = 0.0f;
            surface.Coat.Roughness = 0.0f;
            surface.Coat.F0 = 0.0f;
        }
    }
}

#endif // MATERIAL_COMMON_HLSL
