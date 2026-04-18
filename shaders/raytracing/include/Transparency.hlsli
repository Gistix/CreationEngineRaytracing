#ifndef TRANSPARENCY_HLSLI
#define TRANSPARENCY_HLSLI

#include "include/Common.hlsli"
#include "include/Common/BRDF.hlsli"
#include "include/PBR.hlsli"
#include "raytracing/include/Common.hlsli"

#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Geometry.hlsli"

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

bool ConsiderTransparentMaterial(uint instanceIndex, uint geometryIndex, uint primitiveIndex, float2 barycentrics, inout uint randomSeed)
{
    Mesh mesh = GetMesh(instanceIndex, geometryIndex);
    
    Vertex v0, v1, v2;
    GetVertices(mesh.GeometryIdx, primitiveIndex, v0, v1, v2);
    
    float3 uvw = GetBary(barycentrics);

    Material material = mesh.Material;   
    
    if (material.ShaderType == ShaderType::Water) {
        return true;
    }
    
    float2 texCoord = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
    
    float alpha = Textures[NonUniformResourceIndex(material.BaseTexture())].SampleLevel(DefaultSampler, texCoord, 0).a;
    
    alpha *= material.BaseColor().a;
    
    if ((material.ShaderFlags & ShaderFlags::kVertexAlpha) && !(material.ShaderFlags & ShaderFlags::kTreeAnim))
        alpha *= Interpolate(v0.Color.unpack().a, v1.Color.unpack().a, v2.Color.unpack().a, uvw);

    [branch]
    if (material.AlphaFlags & AlphaFlags::Test)
    {
        if (alpha < material.AlphaThreshold)
            return false;
    }
    
    if (material.AlphaFlags & AlphaFlags::Blend)
    {
        float rnd = Random(randomSeed);
        if (alpha < rnd)
            return false;
    }
    
    return true;
}

bool ConsiderTransparentMaterialShadow(uint instanceIndex, uint geometryIndex, uint primitiveIndex, float2 barycentrics, inout uint randomSeed, in float3 direction, float hitDistance, inout float3 transmitanceInOut)
{
    Instance instance;
    Mesh mesh = GetMesh(instanceIndex, geometryIndex, instance);
    
    Vertex v0, v1, v2;
    GetVertices(mesh.GeometryIdx, primitiveIndex, v0, v1, v2);
    
    float3 uvw = GetBary(barycentrics);

    Material material = mesh.Material;

#if defined(EFFECT_PASSTHROUGH)      
    if (material.ShaderType == ShaderType::Effect)
        return false;
#endif
    
    float2 texCoord = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

    if (material.ShaderType == ShaderType::Water)
    {
        float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) mesh.Transform);

        float handedness = Interpolate(v0.Handedness, v1.Handedness, v2.Handedness, uvw);
        
        float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));        
        float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
        float3 bitangentWS = cross(tangentWS, normalWS) * handedness;        
        
        Surface surface = (Surface)0;
        WaterMaterial(surface, texCoord, tangentWS, bitangentWS, handedness, material);
        
        float3 transmittance = exp(-surface.VolumeAbsorption * hitDistance);

        float3 viewDir = -normalize(direction);
        float NdotV = max(abs(dot(surface.Normal, viewDir)), 0.01f);
        float3 F = BRDF::F_Schlick(surface.F0, NdotV);
        transmittance *= (1.0f - F) / (1.0f + F);

        transmitanceInOut *= transmittance;
        return false;        
    }else
    {   
        float alpha = Textures[NonUniformResourceIndex(material.BaseTexture())].SampleLevel(DefaultSampler, texCoord, 0).a;
    
        alpha *= material.BaseColor().a;
    
        if ((material.ShaderFlags & ShaderFlags::kVertexAlpha) && !(material.ShaderFlags & ShaderFlags::kTreeAnim))
            alpha *= Interpolate(v0.Color.unpack().a, v1.Color.unpack().a, v2.Color.unpack().a, uvw);
        
        [branch]
        if (material.AlphaFlags & AlphaFlags::Test)
        {
            if (alpha < material.AlphaThreshold)
                return false;
        }
    
        if (material.AlphaFlags & AlphaFlags::Blend)
        {
            float rnd = Random(randomSeed);
            if (rnd > alpha)
                return false;
        }
        
        if (((material.AlphaFlags & AlphaFlags::Transmission)) || (material.ShaderFlags & ShaderFlags::kRefraction))
        {
            float3 transmittance = 1.0f;
            [branch]
            if (material.ShaderFlags & ShaderFlags::kRefraction)
            {
                transmittance = 1.0f; // fully transparent glass
            }
            else
            {
                float3 baseColor = Textures[NonUniformResourceIndex(material.BaseTexture())].SampleLevel(DefaultSampler, texCoord, 0).rgb;
                baseColor *= material.BaseColor().rgb;
                transmittance = lerp(float3(1.0f, 1.0f, 1.0f), baseColor, alpha);
            }

            float3 F0 = 0.04f;

            float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) mesh.Transform);

            float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
            float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
            float3 bitangentWS = cross(tangentWS, normalWS) * Interpolate(v0.Handedness, v1.Handedness, v2.Handedness, uvw);

            Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
            float3 normal = normalTexture.SampleLevel(DefaultSampler, texCoord, 0).xyz;

            float handedness = (dot(cross(normalWS, tangentWS), bitangentWS) < 0.0f) ? -1.0f : 1.0f;

            float3 Normal, Tangent, Bitangent;

            NormalMap(
                    normal,
                    handedness,
                    normalWS, tangentWS, bitangentWS,
                    Normal, Tangent, Bitangent
                );

            float3 viewDir = -normalize(direction);
            float NdotV = abs(dot(Normal, viewDir));

            float3 F = BRDF::F_Schlick(F0, NdotV);
            transmittance *= (1.0f - F) / (1.0f + F);

            transmitanceInOut *= transmittance;
            return false;
        }
    
        if ((material.Feature == Feature::kGlowMap || material.PBRFlags & PBR::Flags::HasEmissive) && material.ShaderFlags & ShaderFlags::kAssumeShadowmask) // only window for now
        {
            float3 transmittance = 0.0f;
            float3 F0 = 0.04f;
            [branch]
            if (material.Feature == Feature::kGlowMap)
            {
                transmittance = Textures[NonUniformResourceIndex(material.GlowTexture())].SampleLevel(DefaultSampler, texCoord, 0).rgb;
                [branch]
                if (material.ShaderFlags & ShaderFlags::kSpecular) {
                    float3 specularColor = 0.0f;

                    [branch]
                    if (material.ShaderFlags & ShaderFlags::kModelSpaceNormals) {
                        Texture2D specularTexture = Textures[NonUniformResourceIndex(material.SpecularTexture())];
                        specularColor = specularTexture.SampleLevel(DefaultSampler, texCoord, 0).r * material.SpecularColor().rgb * material.SpecularColor().a;
                    } else {
                        Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
                        specularColor = normalTexture.SampleLevel(DefaultSampler, texCoord, 0).a * material.SpecularColor().rgb * material.SpecularColor().a;
                    }
                    F0 = clamp(0.08f * specularColor, 0.02f, 0.08f);
                }
            }
            else
            {
                Texture2D rmaosTexture = Textures[NonUniformResourceIndex(material.RMAOSTexture())];
                Texture2D emissiveTexture = Textures[NonUniformResourceIndex(material.EmissiveTexture())];
                float specular = rmaosTexture.SampleLevel(DefaultSampler, texCoord, 0).a;
                float3 emissive = emissiveTexture.SampleLevel(DefaultSampler, texCoord, 0).rgb;
                transmittance = emissive;
                F0 = material.SpecularLevel() * specular;
            }

            float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) mesh.Transform);

            float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
            float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
            float3 bitangentWS = cross(tangentWS, normalWS) * Interpolate(v0.Handedness, v1.Handedness, v2.Handedness, uvw);
        
            Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
            float3 normal = normalTexture.SampleLevel(DefaultSampler, texCoord, 0).xyz;

            float handedness = (dot(cross(normalWS, tangentWS), bitangentWS) < 0.0f) ? -1.0f : 1.0f;

            float3 Normal, Tangent, Bitangent;

            NormalMap(
                    normal,
                    handedness,
                    normalWS, tangentWS, bitangentWS,
                    Normal, Tangent, Bitangent
                );

            float3 viewDir = -normalize(direction);

            float NdotV = abs(dot(Normal, viewDir));

            float3 F = BRDF::F_Schlick(F0, NdotV);
            transmittance *= (1.0f - F) / (1.0f + F);

            transmitanceInOut *= transmittance;
            return false;
        }        
    }
    
    return true;
}

#endif // TRANSPARENCY_HLSLI