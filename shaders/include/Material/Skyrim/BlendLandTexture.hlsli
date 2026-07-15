#ifndef BLEND_LAND_TEXTURE_FUNC_HLSL
#define BLEND_LAND_TEXTURE_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/Surface.hlsli"

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

#endif // BLEND_LAND_TEXTURE_FUNC_HLSL
