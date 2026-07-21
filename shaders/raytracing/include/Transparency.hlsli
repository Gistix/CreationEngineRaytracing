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

#include "interop/Properties.hlsli"
#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"
#include "interop/Material/Skyrim/WaterMaterialData.hlsli"
#include "interop/Material/Skyrim/GlowmapMaterialData.hlsli"
#include "interop/Material/Skyrim/PBRMaterialData.hlsli"

bool ConsiderTransparentMaterial(uint instanceIndex, uint geometryIndex, uint primitiveIndex, float2 barycentrics, inout uint randomSeed)
{
    Instance instance;
    Mesh mesh = GetMesh(instanceIndex, geometryIndex, instance);
    
    Vertex v0, v1, v2;
    GetVertices(mesh, primitiveIndex, v0, v1, v2);
    
    float3 uvw = GetBary(barycentrics);

    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
    
    if (material.Type == Type::Water) {
        return true;
    }
    
    float2 texCoord = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
    
    float alpha = Textures[NonUniformResourceIndex(material.DiffuseTexture)].SampleLevel(DefaultSampler, texCoord, 0).a;
    
    alpha *= mesh.Properties.Alpha * instance.Alpha;
    
    if ((mesh.Properties.ShaderFlags & ShaderFlags::kVertexAlpha) && !(mesh.Properties.ShaderFlags & ShaderFlags::kTreeAnim))
        alpha *= Interpolate(v0.Color.unpack().a, v1.Color.unpack().a, v2.Color.unpack().a, uvw);

    [branch]
    if (mesh.Properties.AlphaFlags & AlphaFlags::Test)
    {
        if (alpha < mesh.Properties.AlphaThreshold)
            return false;
    }

	if (mesh.Properties.AlphaFlags & AlphaFlags::Additive)
		alpha = 0.0f;
    
    if (mesh.Properties.AlphaFlags & AlphaFlags::Blend)
    {
        float rnd = Random(randomSeed);
        if (alpha < rnd)
            return false;
    }
    
    return true;
}

float3 ComputeShadowNormal(
    Instance instance, Mesh mesh, Transform meshTransform,
    Vertex v0, Vertex v1, Vertex v2, float3 uvw,
    LightingMaterialData material, float2 texCoord)
{
    float3x3 objectToWorld3x3 = mul((float3x3)instance.Transform, (float3x3)meshTransform.Transform);
    float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
    float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
    float3 bitangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));
    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
    float3 normal = normalTexture.SampleLevel(DefaultSampler, texCoord, 0).xyz;
    float3 N, T, B;
    NormalMap(normal, normalWS, tangentWS, bitangentWS, N, T, B);
    return N;
}

void ApplyFresnelTransmittance(
    float3 normal, float3 F0, float3 direction,
    inout float3 transmittance, inout float3 transmitanceInOut)
{
    float3 viewDir = -normalize(direction);
    float NdotV = abs(dot(normal, viewDir));
    float3 F = BRDF::F_Schlick(F0, NdotV);
    transmittance *= (1.0f - F) / (1.0f + F);
    transmitanceInOut *= transmittance;
}

bool ConsiderTransparentMaterialShadow(uint instanceIndex, uint geometryIndex, uint primitiveIndex, float2 barycentrics, inout uint randomSeed, in float3 direction, float hitDistance, inout float3 transmitanceInOut)
{
    Instance instance;
    Mesh mesh = GetMesh(instanceIndex, geometryIndex, instance);
    Transform meshTransform = Transforms[NonUniformResourceIndex(mesh.TransformID)];
    
    Vertex v0, v1, v2;
    GetVertices(mesh, primitiveIndex, v0, v1, v2);
    
    float3 uvw = GetBary(barycentrics);

    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());

#if defined(EFFECT_PASSTHROUGH)      
    if (material.Type == Type::Effect)
        return false;
#endif
    
    float2 texCoord = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

    if (material.Type == Type::Water)
    {
        float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) meshTransform.Transform);

        float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));        
        float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
        float3 bitangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));    
        
        Surface surface = (Surface)0;
        WaterMaterial(surface, texCoord, tangentWS, bitangentWS, mesh);
        
        float3 transmittance = exp(-surface.VolumeAbsorption * hitDistance);
        ApplyFresnelTransmittance(surface.Normal, surface.F0, direction, transmittance, transmitanceInOut);
        return false;        
    }else
    {   
        float alpha = Textures[NonUniformResourceIndex(material.DiffuseTexture)].SampleLevel(DefaultSampler, texCoord, 0).a;
    
        alpha *= mesh.Properties.Alpha * instance.Alpha;
    
        if ((mesh.Properties.ShaderFlags & ShaderFlags::kVertexAlpha) && !(mesh.Properties.ShaderFlags & ShaderFlags::kTreeAnim))
            alpha *= Interpolate(v0.Color.unpack().a, v1.Color.unpack().a, v2.Color.unpack().a, uvw);
        
        [branch]
        if (mesh.Properties.AlphaFlags & AlphaFlags::Test)
        {
            if (alpha < mesh.Properties.AlphaThreshold)
                return false;
        }

        if (mesh.Properties.AlphaFlags & AlphaFlags::Additive)
            alpha = 0.0f;
    
        if (mesh.Properties.AlphaFlags & AlphaFlags::Blend)
        {
            float rnd = Random(randomSeed);
            if (rnd > alpha)
                return false;
        }
        
        if (((mesh.Properties.AlphaFlags & AlphaFlags::Transmission)) || (mesh.Properties.ShaderFlags & ShaderFlags::kRefraction))
        {
            float3 transmittance = 1.0f;
            [branch]
            if (mesh.Properties.ShaderFlags & ShaderFlags::kRefraction)
            {
                transmittance = 1.0f; // fully transparent glass
            }
            else
            {
                float3 baseColor = Textures[NonUniformResourceIndex(material.DiffuseTexture)].SampleLevel(DefaultSampler, texCoord, 0).rgb;
                transmittance = lerp(float3(1.0f, 1.0f, 1.0f), baseColor, alpha);
            }

            float3 Normal = ComputeShadowNormal(instance, mesh, meshTransform, v0, v1, v2, uvw, material, texCoord);
            ApplyFresnelTransmittance(Normal, 0.04f, direction, transmittance, transmitanceInOut);
            return false;
        }
    
        if ((material.Feature == Feature::kGlowMap || material.Type == Type::TruePBR) && mesh.Properties.ShaderFlags & ShaderFlags::kAssumeShadowmask)
        {
            float3 transmittance = 0.0f;
            float3 F0 = 0.04f;
            [branch]
            if (material.Feature == Feature::kGlowMap)
            {
                GlowmapMaterialDataExtra glow = Materials[0].Load<GlowmapMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
                transmittance = Textures[NonUniformResourceIndex(glow.GlowTexture)].SampleLevel(DefaultSampler, texCoord, 0).rgb;
                [branch]
                if (mesh.Properties.ShaderFlags & ShaderFlags::kSpecular) {
                    float3 specularColor = 0.0f;

                    [branch]
                    if (mesh.Properties.ShaderFlags & ShaderFlags::kModelSpaceNormals) {
                        Texture2D specularTexture = Textures[NonUniformResourceIndex(material.SpecularBackLightingTexture)];
                        specularColor = specularTexture.SampleLevel(DefaultSampler, texCoord, 0).r * material.SpecularColor * material.SpecularColorScale;
                    } else {
                        Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
                        specularColor = normalTexture.SampleLevel(DefaultSampler, texCoord, 0).a * material.SpecularColor * material.SpecularColorScale;
                    }
                    F0 = clamp(0.08f * specularColor, 0.02f, 0.08f);
                }
            }
            else
            {
                PBRMaterialData pbr = Materials[0].Load<PBRMaterialData>(mesh.GetMaterialOffset());
                Texture2D rmaosTexture = Textures[NonUniformResourceIndex(pbr.RMAOSTexture)];
                Texture2D emissiveTexture = Textures[NonUniformResourceIndex(pbr.EmissiveTexture)];
                float specular = rmaosTexture.SampleLevel(DefaultSampler, texCoord, 0).a;
                float3 emissive = emissiveTexture.SampleLevel(DefaultSampler, texCoord, 0).rgb;
                transmittance = emissive;
                F0 = pbr.SpecularLevel * specular;
            }

            float3 Normal = ComputeShadowNormal(instance, mesh, meshTransform, v0, v1, v2, uvw, material, texCoord);
            ApplyFresnelTransmittance(Normal, F0, direction, transmittance, transmitanceInOut);
            return false;
        }        
    }
    
    return true;
}

#endif // TRANSPARENCY_HLSLI