#ifndef LAND_MATERIAL_FUNC_HLSL
#define LAND_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Surface.hlsli"
#include "include/Utils/VanillaToPBR.hlsli"
#include "interop/Properties.hlsli"
#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"
#include "interop/Material/Skyrim/PBRLandscapeMaterialData.hlsli"
#include "interop/Material/Skyrim/LandscapeMaterialData.hlsli"
#include "include/Wetness.hlsli"

#include "include/Material/Skyrim/Common.hlsli"
#include "include/Material/Skyrim/BlendLandTexture.hlsli"

void LandMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, float3 normalWS, float3 tangentWS, float3 bitangentWS, float4 landBlend0, float4 landBlend1, in Mesh mesh)
{
    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
    float mipLevel = surface.MipLevel;

    uint16_t diffTex0, diffTex1, diffTex2, diffTex3, diffTex4, diffTex5;
    uint16_t normTex0, normTex1, normTex2, normTex3, normTex4, normTex5;
    uint16_t rmaosTex0, rmaosTex1, rmaosTex2, rmaosTex3, rmaosTex4, rmaosTex5;
    uint16_t overlayTex, noiseTex;
    float roughness0, roughness1, roughness2, roughness3, roughness4, roughness5;
    float specular0, specular1, specular2, specular3, specular4, specular5;
    uint pbrFlags = 0;

    [branch]
    if (material.Type == Type::TruePBR)
    {
        PBRLandscapeMaterialData pbrLand = Materials[0].Load<PBRLandscapeMaterialData>(mesh.GetMaterialOffset());

        diffTex0  = pbrLand.BaseColorTexture0;  
        diffTex1  = pbrLand.BaseColorTexture1;
        diffTex2  = pbrLand.BaseColorTexture2;  
        diffTex3  = pbrLand.BaseColorTexture3;
        diffTex4  = pbrLand.BaseColorTexture4;  
        diffTex5  = pbrLand.BaseColorTexture5;

        normTex0  = pbrLand.NormalTexture0;     
        normTex1  = pbrLand.NormalTexture1;
        normTex2  = pbrLand.NormalTexture2;     
        normTex3  = pbrLand.NormalTexture3;
        normTex4  = pbrLand.NormalTexture4;    
        normTex5  = pbrLand.NormalTexture5;

        rmaosTex0 = pbrLand.RMAOSTexture0;      
        rmaosTex1 = pbrLand.RMAOSTexture1;
        rmaosTex2 = pbrLand.RMAOSTexture2;      
        rmaosTex3 = pbrLand.RMAOSTexture3;
        rmaosTex4 = pbrLand.RMAOSTexture4;      
        rmaosTex5 = pbrLand.RMAOSTexture5;

        overlayTex = pbrLand.OverlayTexture;
        noiseTex   = pbrLand.NoiseTexture;

        roughness0 = pbrLand.RoughnessScale0; 
        roughness1 = pbrLand.RoughnessScale1;
        roughness2 = pbrLand.RoughnessScale2; 
        roughness3 = pbrLand.RoughnessScale3;
        roughness4 = pbrLand.RoughnessScale4; 
        roughness5 = pbrLand.RoughnessScale5;

        specular0  = pbrLand.SpecularLevel0;  
        specular1  = pbrLand.SpecularLevel1;
        specular2  = pbrLand.SpecularLevel2;  
        specular3  = pbrLand.SpecularLevel3;
        specular4  = pbrLand.SpecularLevel4;  
        specular5  = pbrLand.SpecularLevel5;

        pbrFlags = pbrLand.PBRFlags;
    }
    else
    {
        LandscapeMaterialDataExtra land = Materials[0].Load<LandscapeMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);

        diffTex0  = material.DiffuseTexture;  
        diffTex1  = land.DiffuseTexture1;
        diffTex2  = land.DiffuseTexture2;     
        diffTex3  = land.DiffuseTexture3;
        diffTex4  = land.DiffuseTexture4;     
        diffTex5  = land.DiffuseTexture5;

        normTex0  = material.NormalTexture;   
        normTex1  = land.NormalTexture1;
        normTex2  = land.NormalTexture2;      
        normTex3  = land.NormalTexture3;
        normTex4  = land.NormalTexture4;      
        normTex5  = land.NormalTexture5;

        overlayTex = land.OverlayTexture;
        noiseTex   = land.NoiseTexture;

        float rough = ShininessToRoughness(material.SpecularPower);
        roughness0 = roughness1 = roughness2 = roughness3 = roughness4 = roughness5 = rough;
    }

    Texture2D overlayTexture = Textures[NonUniformResourceIndex(overlayTex)];
    Texture2D noiseTexture = Textures[NonUniformResourceIndex(noiseTex)];

    float totalWeight = landBlend0.x + landBlend0.y + landBlend0.z +
                        landBlend0.w + landBlend1.x + landBlend1.y;

    landBlend0 /= totalWeight;
    landBlend1.xy /= totalWeight;

    float4 blendedNormal = 
        BlendLandTexture(normTex0,  texCoord0, landBlend0.x, mipLevel) + 
        BlendLandTexture(normTex1,  texCoord0, landBlend0.y, mipLevel) +
        BlendLandTexture(normTex2,  texCoord0, landBlend0.z, mipLevel) + 
        BlendLandTexture(normTex3,  texCoord0, landBlend0.w, mipLevel) +
        BlendLandTexture(normTex4,  texCoord0, landBlend1.x, mipLevel) + 
        BlendLandTexture(normTex5,  texCoord0, landBlend1.y, mipLevel);

    float specularStrength = blendedNormal.a;

    float4 land1 = BlendLandTexture(diffTex0, texCoord0, landBlend0.x, mipLevel);
    float4 land2 = BlendLandTexture(diffTex1, texCoord0, landBlend0.y, mipLevel);
    float4 land3 = BlendLandTexture(diffTex2, texCoord0, landBlend0.z, mipLevel);
    float4 land4 = BlendLandTexture(diffTex3, texCoord0, landBlend0.w, mipLevel);
    float4 land5 = BlendLandTexture(diffTex4, texCoord0, landBlend1.x, mipLevel);
    float4 land6 = BlendLandTexture(diffTex5, texCoord0, landBlend1.y, mipLevel);

    float4 blendedLand = float4(0, 0, 0, 0);

    [branch]
    if (material.Type == Type::TruePBR)
    {
        blendedLand += (pbrFlags & PBR::TerrainFlags::LandTile0PBR) ? PBRColorScale(land1) : VanillaDiffuseColor(land1);
        blendedLand += (pbrFlags & PBR::TerrainFlags::LandTile1PBR) ? PBRColorScale(land2) : VanillaDiffuseColor(land2);
        blendedLand += (pbrFlags & PBR::TerrainFlags::LandTile2PBR) ? PBRColorScale(land3) : VanillaDiffuseColor(land3);
        blendedLand += (pbrFlags & PBR::TerrainFlags::LandTile3PBR) ? PBRColorScale(land4) : VanillaDiffuseColor(land4);
        blendedLand += (pbrFlags & PBR::TerrainFlags::LandTile4PBR) ? PBRColorScale(land5) : VanillaDiffuseColor(land5);
        blendedLand += (pbrFlags & PBR::TerrainFlags::LandTile5PBR) ? PBRColorScale(land6) : VanillaDiffuseColor(land6);

        blendedLand.rgb *= saturate(vertexColor.rgb / max(max(vertexColor.r, vertexColor.g), vertexColor.b));

        float4 rmaos = float4(0, 0, 0, 0);
        rmaos += BlendLandTexture(rmaosTex0, texCoord0, landBlend0.x, mipLevel) * float4(roughness0, 1.0f, 1.0f, specular0);
        rmaos += BlendLandTexture(rmaosTex1, texCoord0, landBlend0.y, mipLevel) * float4(roughness1, 1.0f, 1.0f, specular1);
        rmaos += BlendLandTexture(rmaosTex2, texCoord0, landBlend0.z, mipLevel) * float4(roughness2, 1.0f, 1.0f, specular2);
        rmaos += BlendLandTexture(rmaosTex3, texCoord0, landBlend0.w, mipLevel) * float4(roughness3, 1.0f, 1.0f, specular3);
        rmaos += BlendLandTexture(rmaosTex4, texCoord0, landBlend1.x, mipLevel) * float4(roughness4, 1.0f, 1.0f, specular4);
        rmaos += BlendLandTexture(rmaosTex5, texCoord0, landBlend1.y, mipLevel) * float4(roughness5, 1.0f, 1.0f, specular5);

        surface.Roughness = saturate(rmaos.x);
        surface.Metallic = saturate(rmaos.y);
        surface.AO = rmaos.z;
        surface.F0 = rmaos.w;
    }
    else if (material.Type == Type::Lighting)
    {
        blendedLand = VanillaDiffuseColor(land1 + land2 + land3 + land4 + land5 + land6);
        blendedLand.rgb *= VanillaDiffuseColor(vertexColor.rgb);

        float3 specularColor = material.SpecularColor * specularStrength;

        float roughnessFromShininess = 
            roughness0 * landBlend0.x + roughness1 * landBlend0.y +
            roughness2 * landBlend0.z + roughness3 * landBlend0.w +
            roughness4 * landBlend1.x + roughness5 * landBlend1.y;

        float roughnessFromSpecularTexture = pow(1.0f - specularStrength, 2);

        surface.Roughness = lerp(roughnessFromSpecularTexture, roughnessFromShininess, specularStrength);
        surface.F0 = clamp(0.08f * specularColor * material.SpecularColorScale, 0.02f, 0.08f);
    }

    surface.Albedo = blendedLand.rgb;

    NormalMap(
        blendedNormal.xyz,
        normalWS, tangentWS, bitangentWS,
        surface.Normal, surface.Tangent, surface.Bitangent
    );

    {
        Wetness::WetnessParams wetnessParams = Wetness::ComputeWetness(
            surface.Position,
            normalWS,
            surface.Normal,
            Camera.Position,
            Camera.WaterData,
            Features.WetnessEffects,
            false,
            surface.Primary);
        Wetness::ApplyWetness(surface, wetnessParams);
    }
}

#endif // LAND_MATERIAL_FUNC_HLSL
