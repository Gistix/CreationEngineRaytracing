#ifndef LIGHTING_MATERIAL_FUNC_HLSL
#define LIGHTING_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Surface.hlsli"
#include "include/Utils/VanillaToPBR.hlsli"
#include "interop/Properties.hlsli"
#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Fallout4/LightingMaterialData.hlsli"
#include "interop/Material/Fallout4/EnvmapMaterialData.hlsli"
#include "interop/Material/Fallout4/EyeMaterialData.hlsli"
#include "interop/Material/Fallout4/GlowmapMaterialData.hlsli"

#include "include/Material/Fallout4/Common.hlsli"

void LightingMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, in float3 normalWS, in float3 tangentWS, in float3 bitangentWS, in Mesh mesh, Properties props, float4 boneRotation, float3 viewDir, float dist)
{
    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());

    Texture2D diffuseTexture = Textures[NonUniformResourceIndex(material.DiffuseTexture)];
    float4 diffuse = diffuseTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);

    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
    float3 normalMap = normalTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel).xyz;

    Texture2D specMaskTexture = Textures[NonUniformResourceIndex(material.SmoothnessSpecMaskTexture)];
    float4 specMask = specMaskTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);

    surface.Albedo = diffuse.xyz * vertexColor.xyz;
    
    if (props.ShaderFlags & ShaderFlags::kModelSpaceNormals)
    {
        // Swizzle matches vanilla shaders        
        normalMap = normalize(normalMap.xzy * 2.0f - 1.0f);
        
        if (mesh.Type == MeshType::Skinned || mesh.Type == MeshType::Dynamic)
        {
            surface.Normal = RotateByQuaternion(normalMap, boneRotation);
        }
        else
        {
            surface.Normal = normalMap;
        }
        
        CreateOrthonormalBasis(surface.Normal, surface.Tangent, surface.Bitangent);
        
        // Use shading values since the geometry ones aren't available
        surface.GeomNormal = surface.Normal;
        surface.GeomTangent = surface.Tangent;
    }
    else
    {
        NormalMap(
            normalMap.xy,
            normalWS, tangentWS, bitangentWS,
            surface.Normal, surface.Tangent, surface.Bitangent
        );
    }
    
    // Matches 1:1 with PBR
    surface.Roughness = 1.0f - saturate(material.Smoothness * specMask.y);
    
    [branch]
    if (material.Type == Type::TruePBR)
    {
        const float roughnessScale  = material.RefractionPower;
        const float roughnessBias = material.FresnelPower;
        const float metallicMin = material.RimLightPower;
        const float metallicMax = material.BackLightPower;
        const float metallicScale = material.MetallicScale;
        const float3 albedoFactor = material.SpecularColor;

        surface.Roughness = clamp(surface.Roughness * roughnessScale + roughnessBias, 0.04, 0.99);

        float specInt = specMask.x * material.SpecularColorScale;
        float effMax = max(metallicMax, metallicMin + 0.02);
        float mRaw = saturate((specInt - metallicMin) / (effMax - metallicMin));
        surface.Metallic = clamp(mRaw * mRaw * (3.0 - 2.0 * mRaw) * metallicScale, 0.0, 1.0);

        surface.Albedo *= ((1.0f - surface.Metallic) + surface.Metallic * albedoFactor);
    }
    else
    {
        // SpecMask B channel is unused by the game, so we can safely use it to store our metallic map
        surface.Metallic = specMask.z;
        surface.Albedo = lerp(surface.Albedo, material.SpecularColor, surface.Metallic);
    }
    
    if (props.ShaderFlags & ShaderFlags::kOwnEmit)
    {
        surface.Emissive = props.EmissiveColor.rgb * props.EmissiveColor.a * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        
        [branch]
        if (material.Feature == Feature::kGlowMap)
        {
            GlowmapMaterialDataExtra glowData = Materials[0].Load<GlowmapMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            Texture2D glowTexture = Textures[NonUniformResourceIndex(glowData.GlowTexture)];
            surface.Emissive *= glowTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel).rgb;
        }       
    }
    
    float alpha = diffuse.a * material.MaterialAlpha;
    
    [branch]
    if (props.AlphaFlags != AlphaFlags::None)
    {
        alpha *= props.Alpha;
        
        [branch]
        if (props.AlphaFlags & AlphaFlags::Transmission)
        {
            surface.TransmissionColor = lerp(float3(1.0f, 1.0f, 1.0f), surface.Albedo, alpha);
            surface.Albedo *= alpha;
            surface.Metallic *= alpha;
            surface.SpecTrans = 1.0f;
            surface.IsThinSurface = true;
        }

        [branch]
        if (props.AlphaFlags & AlphaFlags::Additive)
        {
            surface.Albedo = 0.0f;
            surface.Metallic = 0.0f;
            surface.Roughness = 0.0f;
            surface.TransmissionColor = 1.0f;
            surface.SpecTrans = 1.0f;
            surface.F0 = 0.04f;
        }
    }
}

#endif // LIGHTING_MATERIAL_FUNC_HLSL

