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
    
    [branch]
    if (material.Type == Type::TruePBR)
    {
        const float roughnessScale  = material.RefractionPower;
        const float roughnessBias = material.FresnelPower;
        const float metallicMin = material.RimLightPower;
        const float metallicMax = material.BackLightPower;
        const float metallicScale = material.MetallicScale;
        const float3 albedoFactor = material.SpecularColor;

        float gloss    = saturate(material.Smoothness * specMask.y);
        float power    = exp2(gloss * 10.0 + 1.0);
        float rawRough = pow(2.0 / (power + 2.0), 0.25);
        surface.Roughness = clamp(rawRough * roughnessScale + roughnessBias, 0.04, 0.99);

        if (material.Feature == Feature::kEnvironmentMap)
        {
            float specInt = specMask.x * material.SpecularColorScale;
            float effMax = max(metallicMax, metallicMin + 0.02);
            float mRaw = saturate((specInt - metallicMin) / (effMax - metallicMin));
            surface.Metallic = clamp(mRaw * mRaw * (3.0 - 2.0 * mRaw) * metallicScale, 0.0, 1.0);

            surface.Albedo = surface.Albedo * albedoFactor;
        }
    }
    else
    {
        surface.Roughness = 1.0f - saturate(material.Smoothness * specMask.y);
    
        [branch]
        if (props.ShaderFlags & ShaderFlags::kEnvMap || props.ShaderFlags & ShaderFlags::kEyeReflect)
        {
            uint16_t envTexIndex;
            float envScale = 1.0f;
        
            if (material.Feature == Feature::kEye)
            {
                EyeMaterialDataExtra eye = Materials[0].Load < EyeMaterialDataExtra > (mesh.GetMaterialOffset() + kLightingSize);
                envTexIndex = eye.EnvironmentTexture;
                envScale = eye.EnvironmentScale;
            }
            else
            {
                EnvmapMaterialDataExtra envMap = Materials[0].Load < EnvmapMaterialDataExtra > (mesh.GetMaterialOffset() + kLightingSize);
                envTexIndex = envMap.EnvironmentTexture;
                envScale = envMap.EnvironmentScale;
            }

            TextureCube envCubemap = CubeTextures[NonUniformResourceIndex(envTexIndex)];
            float4 envColorBase = envCubemap.SampleLevel(DefaultSampler, float3(1.0, 0.0, 0.0), 15);
        
            float envMask = specMask.x * 3.0;
            float envStrength = envMask * min(1.0 / rsqrt(saturate(specMask.y - 0.3)), 1.0) * material.SpecularColorScale;
        
            surface.Metallic = saturate(envStrength * envScale);
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

