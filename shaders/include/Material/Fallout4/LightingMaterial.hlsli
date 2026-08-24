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

float EstimateMetallic(float3 albedo, float3 specularColor)
{
    float3 d = albedo - 0.04.xxx;
    float3 n = specularColor - 0.04.xxx;

    float3 m = n / max(abs(d), 1e-5);

    // Least-squares solution across RGB.
    float metallic = dot(d, n) / max(dot(d, d), 1e-5);

    return saturate(metallic);
}

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
            CreateOrthonormalBasis(surface.Normal, surface.Tangent, surface.Bitangent);
        }
        else
        {
            surface.Normal = normalMap;
        }
        
        // Use shading values since the geometry ones aren't available
        surface.GeomNormal = surface.Normal;
        surface.GeomTangent = surface.Tangent;
    }
    else
    {
        NormalMap(
            normalMap,
            normalWS, tangentWS, bitangentWS,
            surface.Normal, surface.Tangent, surface.Bitangent
        );
    }
    
    surface.Normal = -surface.Normal;
    surface.Tangent = -surface.Tangent;
    surface.Bitangent = -surface.Bitangent;
    
    surface.Roughness = 1.0f - saturate(material.Smoothness * specMask.y);
    
    [branch]
    if (props.ShaderFlags & ShaderFlags::kEnvMap || props.ShaderFlags & ShaderFlags::kEyeReflect)
    {
        uint16_t envMaskTexIndex;
        uint16_t envTexIndex;
        float envScale = 1.0f;
        
        if (material.Feature == Feature::kEye)
        {
            EyeMaterialDataExtra eye = Materials[0].Load<EyeMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            envMaskTexIndex = eye.EnvironmentMaskTexture;
            envTexIndex = eye.EnvironmentTexture;
            envScale = eye.EnvironmentScale;
        }
        else
        {
            EnvmapMaterialDataExtra envMap = Materials[0].Load<EnvmapMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            envMaskTexIndex = envMap.EnvironmentMaskTexture;
            envTexIndex = envMap.EnvironmentTexture;
            envScale = envMap.EnvironmentScale;
        }

        Texture2D envMaskTexture = Textures[NonUniformResourceIndex(envMaskTexIndex)];
        float4 envMask = envMaskTexture.SampleLevel(DefaultSampler, texCoord0, 0);
       
        //surface.Metallic = specMask.x * envMask.x * envScale;
        TextureCube envCubemap = CubeTextures[NonUniformResourceIndex(envTexIndex)];
        float4 envColorBase = envCubemap.SampleLevel(DefaultSampler, float3(1.0, 0.0, 0.0), 15);
        //surface.Metallic = EstimateMetallic(surface.Albedo, envMask.x * envColorBase.xyz * envScale);
    }

    //const float3 specularity = specMask.x * material.SpecularColor * material.SpecularColorScale;
    //urface.F0 = lerp(float3(0.04f, 0.04f, 0.04f), surface.Albedo, surface.Metallic) * saturate(specularity);
  
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

