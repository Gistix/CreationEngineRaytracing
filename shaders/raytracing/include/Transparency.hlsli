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
#if defined(SKYRIM)
#   include "interop/Material/Skyrim/LightingMaterialData.hlsli"
#   include "interop/Material/Skyrim/WaterMaterialData.hlsli"
#   include "interop/Material/Skyrim/GlowmapMaterialData.hlsli"
#   include "interop/Material/Skyrim/PBRMaterialData.hlsli"
#   include "interop/Material/Skyrim/DistantTreeMaterialData.hlsli"
#elif defined(FALLOUT4)
#   include "interop/Material/Fallout4/LightingMaterialData.hlsli"
#   include "interop/Material/Fallout4/EffectMaterialData.hlsli"
#   include "interop/Material/Fallout4/WaterMaterialData.hlsli"
#   include "interop/Material/Fallout4/GlowmapMaterialData.hlsli"
#endif

bool ConsiderTransparentMaterial(uint instanceIndex, uint geometryIndex, uint primitiveIndex, float2 barycentrics, inout uint randomSeed)
{
    Instance instance;
    Mesh mesh = GetMesh(instanceIndex, geometryIndex, instance);

    MaterialBaseData baseMaterial = Materials[0].Load<MaterialBaseData>(mesh.GetMaterialOffset());
    
    if (baseMaterial.Type == Type::Water) {
        return true;
    }
    else
    {
        uint meshSlot = GetMeshSlot(instance, geometryIndex);
        Properties props = GetMeshProperties(meshSlot);
    
        Vertex v0, v1, v2;
        GetVertices(mesh, props, primitiveIndex, v0, v1, v2);
    
        const float3 uvw = GetBary(barycentrics);

        float alpha = props.Alpha * instance.Alpha;

        [branch]
        if (alpha > 0.0f)
        {
            if ((props.ShaderFlags & ShaderFlags::kVertexAlpha) && !(props.ShaderFlags & ShaderFlags::kTreeAnim))
                alpha *= Interpolate(v0.Color.unpack().a, v1.Color.unpack().a, v2.Color.unpack().a, uvw);
        }
        
        [branch]
        if (alpha > 0.0f)
        {
            const float2 texCoord = baseMaterial.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
            
            // Only Fallout 4 has alpha blended effect support
#if defined(FALLOUT4)
            [branch]
            if (baseMaterial.Type == Type::Effect)
            {
                EffectMaterialData effectMaterial = Materials[0].Load<EffectMaterialData>(mesh.GetMaterialOffset());        
                alpha *= Textures[NonUniformResourceIndex(effectMaterial.SourceTexture)].SampleLevel(DefaultSampler, texCoord, 0).r;
            }
            else
#elif defined(SKYRIM)
            if (baseMaterial.Type == Type::DistantTree)
            {
                DistantTreeMaterialData treeMat = Materials[0].Load<DistantTreeMaterialData>(mesh.GetMaterialOffset());
                alpha *= Textures[NonUniformResourceIndex(treeMat.TreeLODAtlas)].SampleLevel(DefaultSampler, texCoord, 0).a;
            }
            else
#endif
            {
                LightingMaterialData lightingMat = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
                alpha *= Textures[NonUniformResourceIndex(lightingMat.DiffuseTexture)].SampleLevel(DefaultSampler, texCoord, 0).a;
            }
        }
        
        [branch]
        if (props.AlphaFlags & AlphaFlags::Test)
        {
            if (alpha < props.AlphaThreshold)
                return false;
        }

        if (props.AlphaFlags & AlphaFlags::Additive)
            alpha = 0.0f;
    
        [branch]
        if (props.AlphaFlags & AlphaFlags::Blend)
        {
            float rnd = Random(randomSeed);
            if (alpha < rnd)
                return false;
        }
    }
    
    return true;
}

float3 ComputeShadowNormal(
    Instance instance, Mesh mesh, Transform meshTransform,
    Vertex v0, Vertex v1, Vertex v2, float3 uvw,
    uint normalTextureIndex, float2 texCoord)
{
    float3x3 objectToWorld3x3 = mul((float3x3)instance.Transform, (float3x3)meshTransform.Transform);
    float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
    float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
    float3 bitangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));
    Texture2D normalTexture = Textures[NonUniformResourceIndex(normalTextureIndex)];
    float3 normal = normalTexture.SampleLevel(DefaultSampler, texCoord, 0).xyz;
    float3 N, T, B;
#if defined(SKYRIM)    
    NormalMap(normal, normalWS, tangentWS, bitangentWS, N, T, B);
#elif defined(FALLOUT4)
    NormalMap(normal.xy, normalWS, tangentWS, bitangentWS, N, T, B);    
#endif    
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
    uint meshSlot = GetMeshSlot(instance, geometryIndex);
    Properties props = GetMeshProperties(meshSlot);
    Transform meshTransform = Transforms[NonUniformResourceIndex(meshSlot)];
    
    Vertex v0, v1, v2;
    GetVertices(mesh, props, primitiveIndex, v0, v1, v2);
    
    const float3 uvw = GetBary(barycentrics);

    MaterialBaseData baseMaterial = Materials[0].Load<MaterialBaseData>(mesh.GetMaterialOffset());
    const float2 texCoord = baseMaterial.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

    if (baseMaterial.Type == Type::Water)
    {
        float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) meshTransform.Transform);

        float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));        
        float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
        float3 bitangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));    
        
        Surface surface = (Surface)0;
        WaterMaterial(surface, texCoord, tangentWS, bitangentWS, mesh, props);
        
        float3 transmittance = exp(-surface.VolumeAbsorption * hitDistance);
        ApplyFresnelTransmittance(surface.Normal, surface.F0, direction, transmittance, transmitanceInOut);
        return false;        
    }
    else
    {
        float alpha = 1.0f;
#if defined(FALLOUT4)
        [branch]
        if (baseMaterial.Type == Type::Effect)
        {
            EffectMaterialData material = Materials[0].Load<EffectMaterialData>(mesh.GetMaterialOffset());
            alpha = Textures[NonUniformResourceIndex(material.SourceTexture)].SampleLevel(DefaultSampler, texCoord, 0).r;
        }
        else
#elif defined(SKYRIM)
        if (baseMaterial.Type == Type::DistantTree)
        {
            DistantTreeMaterialData material = Materials[0].Load<DistantTreeMaterialData>(mesh.GetMaterialOffset());
            alpha = Textures[NonUniformResourceIndex(material.TreeLODAtlas)].SampleLevel(DefaultSampler, texCoord, 0).a;
        } 
        else
#endif
        {
            LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
            alpha = Textures[NonUniformResourceIndex(material.DiffuseTexture)].SampleLevel(DefaultSampler, texCoord, 0).a;
        }
    
        alpha *= props.Alpha * instance.Alpha;
    
        if ((props.ShaderFlags & ShaderFlags::kVertexAlpha) && !(props.ShaderFlags & ShaderFlags::kTreeAnim))
            alpha *= Interpolate(v0.Color.unpack().a, v1.Color.unpack().a, v2.Color.unpack().a, uvw);
        
        [branch]
        if (props.AlphaFlags & AlphaFlags::Test)
        {
            if (alpha < props.AlphaThreshold)
                return false;
        }

        if (props.AlphaFlags & AlphaFlags::Additive)
            alpha = 0.0f;
    
        if (props.AlphaFlags & AlphaFlags::Blend)
        {
            float rnd = Random(randomSeed);
            if (rnd > alpha)
                return false;
        }
        
        const bool isTransmissive = ((props.AlphaFlags & AlphaFlags::Transmission) != 0) || ((props.ShaderFlags & ShaderFlags::kRefraction) != 0);
        const bool isWindow = ((props.ShaderFlags & ShaderFlags::kAssumeShadowmask) != 0) &&
#if defined(SKYRIM)
            (baseMaterial.Feature == Feature::kGlowMap || baseMaterial.Type == Type::TruePBR);
#else
            (baseMaterial.Feature == Feature::kGlowMap);
#endif

        if (!isTransmissive && !isWindow)
            return true;

        if (isTransmissive)
        {
            float3 transmittance = 1.0f;
            uint normalTextureIndex = 0;

            [branch]
            if (props.ShaderFlags & ShaderFlags::kRefraction)
            {
                transmittance = 1.0f; // fully transparent glass
                LightingMaterialData lightingMat = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
                normalTextureIndex = lightingMat.NormalTexture;
            }
#if defined(FALLOUT4)
            else if (baseMaterial.Type == Type::Effect)
            {
                transmittance = 1.0f; // fully transparent glass
                EffectMaterialData effectMat = Materials[0].Load<EffectMaterialData>(mesh.GetMaterialOffset());
                normalTextureIndex = effectMat.NormalTexture;
            }
#endif
            else if (baseMaterial.Type == Type::Lighting)
            {
                LightingMaterialData lightingMat = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
                float3 baseColor = Textures[NonUniformResourceIndex(lightingMat.DiffuseTexture)].SampleLevel(DefaultSampler, texCoord, 0).rgb;
                transmittance = lerp(float3(1.0f, 1.0f, 1.0f), baseColor, alpha);
                normalTextureIndex = lightingMat.NormalTexture;
            }

            const float3 normal = ComputeShadowNormal(instance, mesh, meshTransform, v0, v1, v2, uvw, normalTextureIndex, texCoord);
            ApplyFresnelTransmittance(normal, 0.04f, direction, transmittance, transmitanceInOut);
            return false;
        }
    
        if (isWindow)
        {
            float3 transmittance = 0.0f;
            float3 F0 = 0.04f;
            uint normalTextureIndex = 0;

            [branch]
            if (baseMaterial.Feature == Feature::kGlowMap)
            {
                LightingMaterialData lightingMat = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
                normalTextureIndex = lightingMat.NormalTexture;

                GlowmapMaterialDataExtra glow = Materials[0].Load<GlowmapMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
                transmittance = Textures[NonUniformResourceIndex(glow.GlowTexture)].SampleLevel(DefaultSampler, texCoord, 0).rgb;
                
                [branch]
                if (props.ShaderFlags & ShaderFlags::kSpecular) {
                    float3 specularColor = 0.0f;

                    [branch]
                    if (props.ShaderFlags & ShaderFlags::kModelSpaceNormals) {
                        Texture2D specularTexture = Textures[NonUniformResourceIndex(
#if defined(SKYRIM)
                            lightingMat.SpecularBackLightingTexture
#else
                            lightingMat.SmoothnessSpecMaskTexture
#endif
                        )];
                        specularColor = specularTexture.SampleLevel(DefaultSampler, texCoord, 0).r * lightingMat.SpecularColor * lightingMat.SpecularColorScale;
                    } else {
                        Texture2D normalTexture = Textures[NonUniformResourceIndex(lightingMat.NormalTexture)];
                        specularColor = normalTexture.SampleLevel(DefaultSampler, texCoord, 0).a * lightingMat.SpecularColor * lightingMat.SpecularColorScale;
                    }
                    F0 = clamp(0.08f * specularColor, 0.02f, 0.08f);
                }
            }
#if defined(SKYRIM)
            else
            {
                PBRMaterialData pbr = Materials[0].Load<PBRMaterialData>(mesh.GetMaterialOffset());
                Texture2D rmaosTexture = Textures[NonUniformResourceIndex(pbr.RMAOSTexture)];
                Texture2D emissiveTexture = Textures[NonUniformResourceIndex(pbr.EmissiveTexture)];
                float specular = rmaosTexture.SampleLevel(DefaultSampler, texCoord, 0).a;
                float3 emissive = emissiveTexture.SampleLevel(DefaultSampler, texCoord, 0).rgb;
                transmittance = emissive;
                F0 = pbr.SpecularLevel * specular;
                normalTextureIndex = pbr.NormalTexture;
            }
#endif

            float3 normal = ComputeShadowNormal(instance, mesh, meshTransform, v0, v1, v2, uvw, normalTextureIndex, texCoord);
            ApplyFresnelTransmittance(normal, F0, direction, transmittance, transmitanceInOut);
            return false;
        }        
    }
    
    return true;
}

#endif // TRANSPARENCY_HLSLI

