#ifndef LAND_MATERIAL_FUNC_HLSL
#define LAND_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Surface.hlsli"
#include "include/Utils/VanillaToPBR.hlsli"
#include "interop/Properties.hlsli"
#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Fallout4/LandscapeMaterialData.hlsli"

#include "include/Material/Common.hlsli"
#include "include/Material/Fallout4/Common.hlsli"

void LandMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, float3 normalWS, float3 tangentWS, float3 bitangentWS, float4 landBlend0, float4 landBlend1, in Mesh mesh, float3 viewDir, float dist)
{
    const float mipLevel = surface.Geometry.MipLevel;
    
    LandscapeMaterialData material = Materials[0].Load<LandscapeMaterialData>(mesh.GetMaterialOffset());

    uint16_t diffTex0 = NonUniformResourceIndex(material.DiffuseTexture);
    uint16_t diffTex1 = NonUniformResourceIndex(material.DiffuseTexture1);
    uint16_t diffTex2 = NonUniformResourceIndex(material.DiffuseTexture2);
    uint16_t diffTex3 = NonUniformResourceIndex(material.DiffuseTexture3);
    
    uint16_t normalTex0 = NonUniformResourceIndex(material.NormalTexture);
    uint16_t normalTex1 = NonUniformResourceIndex(material.NormalTexture1);
    uint16_t normalTex2 = NonUniformResourceIndex(material.NormalTexture2);
    uint16_t normalTex3 = NonUniformResourceIndex(material.NormalTexture3);
    
    uint16_t smoothSpecTex0 = NonUniformResourceIndex(material.SmoothnessSpecMaskTexture);
    uint16_t smoothSpecTex1 = NonUniformResourceIndex(material.SmoothSpecTexture1);
    uint16_t smoothSpecTex2 = NonUniformResourceIndex(material.SmoothSpecTexture2);
    uint16_t smoothSpecTex3 = NonUniformResourceIndex(material.SmoothSpecTexture3);
    
    float totalWeight = landBlend0.x + landBlend0.y + landBlend0.z + landBlend0.w;

    landBlend0 /= totalWeight;

    float3 blendedAlbedo =
        BlendLandTexture(diffTex0, texCoord0, landBlend0.x, mipLevel).xyz +
        BlendLandTexture(diffTex1, texCoord0, landBlend0.y, mipLevel).xyz +
        BlendLandTexture(diffTex2, texCoord0, landBlend0.z, mipLevel).xyz +
        BlendLandTexture(diffTex3, texCoord0, landBlend0.w, mipLevel).xyz;
       
    
    float2 blendedNormal =
        BlendLandTexture(normalTex0, texCoord0, landBlend0.x, mipLevel).xy +
        BlendLandTexture(normalTex1, texCoord0, landBlend0.y, mipLevel).xy +
        BlendLandTexture(normalTex2, texCoord0, landBlend0.z, mipLevel).xy +
        BlendLandTexture(normalTex3, texCoord0, landBlend0.w, mipLevel).xy;
    
    float blendedSmoothness =
        BlendLandTexture(smoothSpecTex0, texCoord0, landBlend0.x, mipLevel).y +
        BlendLandTexture(smoothSpecTex1, texCoord0, landBlend0.y, mipLevel).y +
        BlendLandTexture(smoothSpecTex2, texCoord0, landBlend0.z, mipLevel).y +
        BlendLandTexture(smoothSpecTex3, texCoord0, landBlend0.w, mipLevel).y;

    surface.Material.Albedo = blendedAlbedo;
     
    NormalMap(
        blendedNormal.xy,
        normalWS, tangentWS, bitangentWS,
        surface.Geometry.Normal, surface.Frame.Tangent, surface.Frame.Bitangent
    );
    
    surface.Material.Roughness = 1.0f - blendedSmoothness;
    surface.Material.Metallic = 0.0f;
}

#endif // LAND_MATERIAL_FUNC_HLSL


