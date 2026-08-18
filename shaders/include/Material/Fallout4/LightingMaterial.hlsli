#ifndef LIGHTING_MATERIAL_FUNC_HLSL
#define LIGHTING_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Surface.hlsli"
#include "include/Utils/VanillaToPBR.hlsli"
#include "interop/Properties.hlsli"
#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Fallout4/LightingMaterialData.hlsli"

#include "include/Material/Fallout4/Common.hlsli"

void LightingMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, in float3 normalWS, in float3 tangentWS, in float3 bitangentWS, in Mesh mesh, Properties props, float4 boneRotation, float3 viewDir, float dist)
{
    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());

    Texture2D diffuseTexture = Textures[NonUniformResourceIndex(material.DiffuseTexture)];
    float4 diffuse = diffuseTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);

    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
    float4 normalMap = normalTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);

    Texture2D specMaskTexture = Textures[NonUniformResourceIndex(material.SmoothnessSpecMaskTexture)];
    float4 specMask = specMaskTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);

    surface.Albedo = ColorToLinear(diffuse.xyz) * ColorToLinear(vertexColor.xyz);
    
    float alpha = diffuse.a * material.MaterialAlpha;
    surface.GeomNormal = normalWS;

    float3 normal = normalMap.xyz * 2.0 - 1.0;
    float3x3 tbnTr = float3x3(tangentWS, bitangentWS, normalWS);
    surface.Normal = normalize(mul(normal, tbnTr));

    // Basic mapping for FO4, avoiding Skyrim's VanillaToPBR logic for now.
    // The mask texture is specular in R and smoothness in G? FO4 typically packs Smoothness in G, Spec in R or inversely?
    // Let's just use the material properties directly.
    float smoothness = material.Smoothness * specMask.y; // Guessing mask channels
    surface.Roughness = ShininessToRoughness(smoothness * 100.0f); // Adjust mapping
    surface.Metallic = material.WetnessControl_Metalness; // From wetness block or standard mask?
    
    // Instead of Surface.Specular, we compute F0
    float specularity = max(material.SpecularColor.r, max(material.SpecularColor.g, material.SpecularColor.b)) * material.SpecularColorScale * specMask.x;
    surface.F0 = lerp(float3(0.04f, 0.04f, 0.04f), surface.Albedo, surface.Metallic) * saturate(specularity);

    
    
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

