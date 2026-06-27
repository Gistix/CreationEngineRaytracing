#ifndef SURFACE_SKYRIM_HLSL
#define SURFACE_SKYRIM_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"

#include "include/Surface.hlsli"

#include "include/Utils/VanillaToPBR.hlsli"

#include "interop/Properties.hlsli"
#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"
#include "interop/Material/Skyrim/PBRMaterialData.hlsli"
#include "interop/Material/Skyrim/HairTintMaterialData.hlsli"
#include "interop/Material/Skyrim/EnvmapMaterialData.hlsli"
#include "interop/Material/Skyrim/GlowmapMaterialData.hlsli"
#include "interop/Material/Skyrim/FacegenMaterialData.hlsli"
#include "interop/Material/Skyrim/FacegenTintMaterialData.hlsli"
#include "interop/Material/Skyrim/EyeMaterialData.hlsli"
#include "interop/Material/Skyrim/LandscapeMaterialData.hlsli"
#include "interop/Material/Skyrim/LODLandscapeMaterialData.hlsli"
#include "interop/Material/Skyrim/PBRLandscapeMaterialData.hlsli"
#include "interop/Material/Skyrim/WaterMaterialData.hlsli"
#include "interop/Material/Skyrim/EffectMaterialData.hlsli"

#include "include/FlowMap.hlsli"
#include "include/Wetness.hlsli"
#include "include/Common/Triplanar.hlsli"

#define LIGHTINGSETTINGS Raytracing
#define HAIRSETTINGS Features.HairSpecular
#define SKINSETTINGS Features.Skin

static const uint kBaseSize = sizeof(MaterialBaseData);
static const uint kLightingSize = sizeof(LightingMaterialData);

void DefaultMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, in float3 normalWS, in float3 tangentWS, in float3 bitangentWS, in Mesh mesh, float4 boneRotation)
{
    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
    float mipLevel = surface.MipLevel;

    const Texture2D baseTexture = Textures[NonUniformResourceIndex(material.DiffuseTexture)];

    const bool clampSampler = mesh.Properties.ShaderFlags & ShaderFlags::kLODLandscape;

    const Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
    
    const bool skinEnabled = (material.Type == Type::Lighting) &&
        (material.Feature == Feature::kFaceGen || material.Feature == Feature::kSkinTint) &&
        SKINSETTINGS.skinParams.w > 0.0f;
    
    float3 normal = clampSampler ? 
        normalTexture.SampleLevel(ClampSampler, texCoord0, mipLevel).xyz : 
        normalTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).xyz;

#if SKIN_DETAIL_NORMAL
    [branch]
    if (SKINSETTINGS.skinDetailParams.w > 0.0f && skinEnabled)
    {
        float2 detailUV = texCoord0 * SKINSETTINGS.skinDetailParams.x * (material.Feature == Feature::kFaceGen ? 1.0f : SKINSETTINGS.skinDetailParams.y);
        float3 detailNormal = float3(SkinDetailNormal.SampleLevel(DefaultSampler, detailUV, mipLevel).xy, 0.5f);
        detailNormal = (detailNormal * 2.0 - 1.0) * SKINSETTINGS.skinDetailParams.z;
        normal = normalize(float3(ReorientNormal(detailNormal, (normal * 2 - 1)).xy, normal.z)) * 0.5f + 0.5f;
    }
#endif

    if (mesh.Properties.ShaderFlags & ShaderFlags::kModelSpaceNormals)
    {
        // Swizzle matches vanilla shaders        
        normal = normalize(normal.xzy * 2.0f - 1.0f);
        
        if (mesh.Type == MeshType::Skinned || mesh.Type == MeshType::Dynamic)
        {
            surface.Normal = RotateByQuaternion(normal, boneRotation);
            CreateOrthonormalBasis(surface.Normal, surface.Tangent, surface.Bitangent);
        }
        else
        {
            surface.Normal = normal;
        }
        
        // Use shading values since the geometry ones aren't available
        surface.GeomNormal = surface.Normal;
        surface.GeomTangent = surface.Tangent;
    }
    else
    {
        NormalMap(
            normal,
            normalWS, tangentWS, bitangentWS,
            surface.Normal, surface.Tangent, surface.Bitangent
        );
    }

    vertexColor.rgb = saturate(vertexColor.rgb / max(max(vertexColor.r, vertexColor.g), vertexColor.b));
    
    const bool isWindows = material.Feature == Feature::kGlowMap && mesh.Properties.ShaderFlags & ShaderFlags::kAssumeShadowmask;
    float3 windowAlpha = float3(0.0f, 0.0f, 0.0f);

    float alpha = 1.0f;
    
    [branch]
    if (material.Type == Type::TruePBR)
    {
        PBRMaterialData pbr = Materials[0].Load<PBRMaterialData>(mesh.GetMaterialOffset());
        Texture2D rmaosTexture = Textures[NonUniformResourceIndex(pbr.RMAOSTexture)];
        Texture2D emissiveTexture = Textures[NonUniformResourceIndex(pbr.EmissiveTexture)];

        float4 albedo = baseTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
        albedo.rgb = PBRColorScale(albedo.rgb);
        alpha = albedo.a;
        
        float4 rmaos = rmaosTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
        float3 emissive = emissiveTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;

        if (isWindows)
        {
            windowAlpha = emissive;
        }

        surface.Albedo = albedo.rgb * vertexColor.rgb;
        surface.Emissive = emissive * EmitColorToLinear(mesh.Properties.EmissiveColor.rgb) * mesh.Properties.EmissiveColor.a * EmitColorMult() * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        surface.Roughness = saturate(rmaos.x * pbr.RoughnessScale);
        surface.Metallic = saturate(rmaos.y);
        surface.AO = rmaos.z;
        surface.F0 = pbr.SpecularLevel * rmaos.w;

        if (pbr.PBRFlags & PBR::Flags::Subsurface)
        {
            Texture2D subsurfaceTexture = Textures[NonUniformResourceIndex(pbr.FeaturesTexture0)];

            float4 subsurfaceColor = subsurfaceTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
            float thickness = subsurfaceColor.a * pbr.FeatureScalar;
            surface.TransmissionColor = surface.Albedo;
            surface.DiffTrans = 0.5f;

            if (!(mesh.Properties.ShaderFlags & ShaderFlags::kTwoSided))
            {
                surface.SubsurfaceData.ScatteringColor = subsurfaceColor.rgb * pbr.FeatureColor.rgb;
                surface.SubsurfaceData.TransmissionColor = surface.Albedo;

                surface.SubsurfaceData.Scale = 40.0f;
                surface.SubsurfaceData.Anisotropy = 0.0f;

                surface.SubsurfaceData.HasSubsurface = any(surface.SubsurfaceData.ScatteringColor) > 0.0f ? 1 : 0;
            }
        }

        // Coat (TwoLayer)
        if (pbr.PBRFlags & PBR::Flags::TwoLayer)
        {
            half4 coatColorParam = pbr.FeatureColor;
            surface.CoatColor = coatColorParam.rgb;
            surface.CoatStrength = coatColorParam.a;
            surface.CoatRoughness = pbr.FeatureScalar;
            surface.CoatF0 = float3(0.04, 0.04, 0.04);

            if (pbr.PBRFlags & PBR::Flags::HasFeatureTexture0)
            {
                Texture2D coatColorTexture = Textures[NonUniformResourceIndex(pbr.FeaturesTexture0)];
                float4 sampledCoat = coatColorTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                surface.CoatColor *= sampledCoat.rgb;
                surface.CoatStrength *= sampledCoat.a;
            }

            if (pbr.PBRFlags & PBR::Flags::HasFeatureTexture1)
            {
                Texture2D coatNormalTexture = Textures[NonUniformResourceIndex(pbr.FeaturesTexture1)];
                float4 sampledCoatNormal = coatNormalTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                surface.CoatRoughness *= sampledCoatNormal.a;

                if (pbr.PBRFlags & PBR::Flags::CoatNormal)
                {
                    NormalMap(
                        sampledCoatNormal.xyz,
                        normalWS, tangentWS, bitangentWS,
                        surface.CoatNormal, surface.CoatTangent, surface.CoatBitangent
                    );
                }
            }
        }

        // Fuzz (OpenPBR §3.7)
        if (pbr.PBRFlags & PBR::Flags::Fuzz)
        {
            half4 fuzzColorWeight = pbr.FeatureColor;
            surface.FuzzColor = fuzzColorWeight.rgb;
            surface.FuzzWeight = fuzzColorWeight.a;

            if (pbr.PBRFlags & PBR::Flags::HasFeatureTexture1)
            {
                Texture2D fuzzTexture = Textures[NonUniformResourceIndex(pbr.FeaturesTexture1)];
                float4 sampledFuzz = fuzzTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                surface.FuzzColor *= sampledFuzz.rgb;
                surface.FuzzWeight *= sampledFuzz.a;
            }
        }

        // Glint (Discrete Stochastic Microfacet Model)
        if (pbr.PBRFlags & PBR::Flags::Glint)
        {
            half4 glintParams = pbr.GlintParameters;
            surface.GlintScreenSpaceScale = glintParams.x;
            surface.GlintLogMicrofacetDensity = glintParams.y;
            surface.GlintMicrofacetRoughness = glintParams.z;
            surface.GlintDensityRandomization = glintParams.w;
            surface.GlintTexCoord = texCoord0;
        }

        // OpenPBR 3.11: Emission sits below the coat and is absorbed.
        // At normal incidence, coat_color = T^2 gives the round-trip absorption.
        if (surface.CoatStrength > 0)
        {
            surface.Emissive *= lerp(float3(1, 1, 1), surface.CoatColor, surface.CoatStrength);
        }
    }
    else if (material.Type == Type::Lighting)
    {
        float4 diffuse = 
            clampSampler ? 
            baseTexture.SampleLevel(ClampSampler, texCoord0, mipLevel) : 
            baseTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
        
        alpha = diffuse.a;
        
        float3 albedo = diffuse.rgb * vertexColor.rgb;
        
        [branch]
        if (mesh.Properties.ShaderFlags & ShaderFlags::kLODLandscape)
        {
            albedo = pow(albedo, Features.LODBlending.LODTerrainGamma) * Features.LODBlending.LODTerrainBrightness;

        }
        else if ((mesh.Properties.ShaderFlags & ShaderFlags::kLODObjects) || (mesh.Properties.ShaderFlags & ShaderFlags::kHDLODObjects))
        {
            albedo = pow(albedo, Features.LODBlending.LODObjectGamma) * Features.LODBlending.LODObjectBrightness;
        }
        
        surface.Albedo = VanillaDiffuseColor(albedo);

        [branch]
        if (material.Feature == Feature::kHairTint)
        {
            HairTintMaterialDataExtra hair = Materials[0].Load<HairTintMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            surface.Albedo *= VanillaDiffuseColor(hair.TintColor);
        }
    
        [branch]
        if (mesh.Properties.ShaderFlags & ShaderFlags::kSpecular)
        {
            float3 specularColor = material.SpecularColor;
            float specularStrength = 0;
            
            [branch]
            if (mesh.Properties.ShaderFlags & ShaderFlags::kModelSpaceNormals)
            {
                Texture2D specularTexture = Textures[NonUniformResourceIndex(material.SpecularBackLightingTexture)];
                specularStrength = specularTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).r;
            }
            else
            {
                Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
                specularStrength = normalTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).a;
            }
            specularColor *= specularStrength;
    
            float roughnessFromShininess = ShininessToRoughness(material.SpecularPower);
            float roughnessFromSpecularTexture = pow(1.0f - specularStrength, 2);

            surface.Roughness = lerp(roughnessFromSpecularTexture, roughnessFromShininess, specularStrength);
            surface.F0 = clamp(0.08f * specularColor * material.SpecularColorScale, 0.02f, 0.08f);
        }
         
        [branch]
        if (mesh.Properties.ShaderFlags & ShaderFlags::kEnvMap || mesh.Properties.ShaderFlags & ShaderFlags::kEyeReflect)
        {
            uint16_t envMaskTexIndex;
            uint16_t envTexIndex;

            if (material.Feature == Feature::kEye) {
                EyeMaterialDataExtra eye = Materials[0].Load<EyeMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
                envMaskTexIndex = eye.EnvironmentMaskTexture;
                envTexIndex = eye.EnvironmentTexture;
            }
            else {
                EnvmapMaterialDataExtra envMap = Materials[0].Load<EnvmapMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
                envMaskTexIndex = envMap.EnvironmentMaskTexture;
                envTexIndex = envMap.EnvironmentTexture;
            }

            Texture2D envMaskTexture = Textures[NonUniformResourceIndex(envMaskTexIndex)];
            float4 envMask = envMaskTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);

            // CM code yoinked from CS
            bool complexMaterial = false;          
            if (Features.ExtendedMaterial.EnableComplexMaterial)
            {
                const float kMaskEpsilon = (4.0f / 255.0f);
                complexMaterial = envMask.w < (1.0f - kMaskEpsilon);
                
		        // Detect texture saved in the wrong format
                if ((abs(envMask.x - envMask.y) < kMaskEpsilon) &&
			        (abs(envMask.x - envMask.z) < kMaskEpsilon) &&
			        (abs(envMask.y - envMask.z) < kMaskEpsilon))
                        complexMaterial = false;
            }
             
            if (complexMaterial)
            {
                surface.Roughness = 1.0f - envMask.y;
                surface.Metallic = envMask.z;
            }
            else
            {
                // Cubemap-based material override
                TextureCube envCubemap = CubeTextures[NonUniformResourceIndex(envTexIndex)];

                // Dynamic Cubemap Creator sets mip 15 at (0,1,0) to black
                float3 envColorTest = envCubemap.SampleLevel(DefaultSampler, float3(0.0, 1.0, 0.0), 15).xyz;
                bool dynamicCubemap = all(envColorTest == 0.0);

                if (dynamicCubemap)
                {
                    float4 envColorBase = envCubemap.SampleLevel(DefaultSampler, float3(1.0, 0.0, 0.0), 15);

                    if (envColorBase.a < 1.0f)
                    {
                        surface.F0 = lerp(surface.F0, ColorToLinear(envColorBase.rgb), envMask.r);
                        surface.Roughness = lerp(surface.Roughness, envColorBase.a, envMask.r);
                    }
                    else
                    {
                        surface.F0 = lerp(surface.F0, float3(1.0, 1.0, 1.0), envMask.r);
                        surface.Roughness = lerp(surface.Roughness, 1.0 / 7.0, envMask.r);
                    }
                }
                else
                {
                    // Static cubemap: use +X face average color as metallic tint
                    float3 faceAvg = envCubemap.SampleLevel(DefaultSampler, float3(1.0, 0.0, 0.0), 15).rgb;
                    surface.F0 = lerp(surface.F0, saturate(ColorToLinear(faceAvg)), envMask.r);
                    surface.Roughness = lerp(surface.Roughness, 0.0f, envMask.r);
                }
            }
        }

        [branch]
        if (material.Feature == Feature::kGlowMap)
        {
            GlowmapMaterialDataExtra glowData = Materials[0].Load<GlowmapMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            Texture2D glowTexture = Textures[NonUniformResourceIndex(glowData.GlowTexture)];
            float3 glow = glowTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;
                
            if (isWindows)
            {
                windowAlpha = glow;
            }
            surface.Emissive = GlowToLinear(glow) * EmitColorToLinear(mesh.Properties.EmissiveColor.rgb) * mesh.Properties.EmissiveColor.a * EmitColorMult() * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        }
        else
        {
            surface.Emissive = surface.Albedo * EmitColorToLinear(mesh.Properties.EmissiveColor.rgb) * mesh.Properties.EmissiveColor.a * EmitColorMult() * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        }

        [branch]
        if (material.Feature == Feature::kFaceGen)
        {
            FacegenMaterialDataExtra facegen = Materials[0].Load<FacegenMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            float3 gammaAlbedo = VanillaDiffuseColorGamma(surface.Albedo);
            
            Texture2D detailTexture = Textures[NonUniformResourceIndex(facegen.DetailTexture)];
            float3 detailColor = detailTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;
            detailColor = float3(3.984375, 3.984375, 3.984375) * (float3(0.00392156886, 0, 0.00392156886) + detailColor);
               
            Texture2D tintTexture = Textures[NonUniformResourceIndex(facegen.TintTexture)];
            float3 tintColor = tintTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;
            tintColor = tintColor * gammaAlbedo * 2.0f;
            tintColor = tintColor - tintColor * gammaAlbedo;
            surface.Albedo = VanillaDiffuseColor((gammaAlbedo * gammaAlbedo + tintColor) * detailColor);
                
        }
        else if (material.Feature == Feature::kSkinTint)
        {
            FacegenTintMaterialDataExtra tintData = Materials[0].Load<FacegenTintMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            float3 gammaAlbedo = VanillaDiffuseColorGamma(surface.Albedo);
            
            float3 tintColor = tintData.TintColor * gammaAlbedo * 2.0f;
            tintColor = tintColor - tintColor * gammaAlbedo;
            surface.Albedo = VanillaDiffuseColor(float3(1.01171875f, 0.99609375f, 1.01171875f) * (gammaAlbedo * gammaAlbedo + tintColor));
        }
        
        [branch]
        if (material.Feature == Feature::kFaceGen || material.Feature == Feature::kSkinTint)
        {
            surface.F0 = 0.02776f;
            surface.Metallic = 0.0f;
            surface.SubsurfaceData.HasSubsurface = 1;
            surface.SubsurfaceData.Anisotropy = -0.5f;

            // Typical skin values
            surface.SubsurfaceData.ScatteringColor = float3(4.820f, 1.690f, 1.090f);
            surface.SubsurfaceData.TransmissionColor = surface.Albedo;
            surface.SubsurfaceData.Scale = 1.f;

            if (skinEnabled)
            {
                Texture2D rfaosTexture = Textures[0]; // TODO: RFAOSTexture — CS skin feature not yet in typed struct
                uint2 rfaosDimensions;
                rfaosTexture.GetDimensions(rfaosDimensions.x, rfaosDimensions.y);
                bool hasValidRFAOS = rfaosDimensions.x > 32 && rfaosDimensions.y > 32;

                surface.Albedo *= SKINSETTINGS.skinParams2.w;
                surface.Roughness = SKINSETTINGS.skinParams.x;
                surface.F0 = SKINSETTINGS.skinParams2.zzz;

                // Skin coat layer (second specular lobe)
                surface.CoatStrength = SKINSETTINGS.skinParams2.x;
                surface.CoatRoughness = SKINSETTINGS.skinParams.y;
                surface.CoatF0 = float3(0.04, 0.04, 0.04);
                surface.CoatNormal = surface.Normal;

                if (hasValidRFAOS)
                {
                    float4 rfaos = rfaosTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                    surface.Roughness = rfaos.x * SKINSETTINGS.physicalParams.x;
                    surface.CoatRoughness = rfaos.x * SKINSETTINGS.physicalParams.y;
                    surface.F0 = 0.08 * rfaos.w * SKINSETTINGS.physicalParams.z;
                    surface.AO = rfaos.z;
                }
            }
        }
        else if (material.Feature == Feature::kEye)
        {
            surface.Roughness = 0.2f;
            surface.F0 = 0.02776f;
            surface.Metallic = 0.0f;
            surface.SubsurfaceData.HasSubsurface = 1;
            surface.SubsurfaceData.Anisotropy = -0.5f;
            
            // Typical eye values
            surface.SubsurfaceData.ScatteringColor = float3(0.482f, 0.169f, 0.109f);
            surface.SubsurfaceData.TransmissionColor = surface.Albedo;
            surface.SubsurfaceData.Scale = 10.f;

            surface.CoatStrength = 1.f;
            surface.CoatRoughness = 0.0f;
            surface.CoatF0 = 0.026f;
        }
        else if (mesh.Properties.ShaderFlags & ShaderFlags::kSoftLighting || mesh.Properties.ShaderFlags & ShaderFlags::kBackLighting)
        {
            surface.TransmissionColor = surface.Albedo;
            surface.DiffTrans = 0.5f;
            
            if (!(mesh.Properties.ShaderFlags & ShaderFlags::kTwoSided) && (mesh.Properties.ShaderFlags & ShaderFlags::kSoftLighting))
            {
                surface.SubsurfaceData.HasSubsurface = 1;
                surface.SubsurfaceData.Anisotropy = -0.5f;

                Texture2D scatterTexture = Textures[NonUniformResourceIndex(material.RimSoftLightingTexture)];
                surface.SubsurfaceData.ScatteringColor = scatterTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb * K_PI;
                surface.SubsurfaceData.TransmissionColor = surface.Albedo;
                surface.SubsurfaceData.Scale = 1.f;
            }
        }

        [branch]
        if (mesh.Properties.ShaderFlags & ShaderFlags::kRefraction) // As glass
        {
            surface.Albedo = float3(0.0f, 0.0f, 0.0f);
            surface.Roughness = 0.0f;
            surface.Emissive = float3(0.0f, 0.0f, 0.0f);
            surface.F0 = 0.04f;
            surface.Metallic = 0.0f;
            surface.TransmissionColor = 1.0f;
            surface.SpecTrans = 1.0f;
            surface.IsThinSurface = true;
            alpha = 0.0f;
        }
    }
    else
    {
        surface.Albedo = float3(1.0f, 0.0f, 1.0f);
    }

    [branch]
    if (mesh.Properties.ShaderFlags & ShaderFlags::kProjectedUV)
    {
        const float4 projectedUVParams = mesh.Properties.ProjectedUVParams0;
        const float4 projectedUVParams2 = mesh.Properties.ProjectedUVParams1;
        const float4 projectedUVParams3 = mesh.Properties.ProjectedUVParams2;
            
        float3 triWeights = Triplanar::GetWeights(surface.GeomNormal, surface.FaceNormal);
        float projNoise = Triplanar::Sample(Textures[0], DefaultSampler, mipLevel, surface.Position, triWeights, projectedUVParams.z).x;
            
        float3 texProj = mesh.Properties.ProjectedUVParams3.xyz;
             
        float vertexAlpha;
        if ((mesh.Properties.ShaderFlags & ShaderFlags::kTreeAnim) || (mesh.Properties.ShaderFlags & ShaderFlags::kHDLODObjects))
            vertexAlpha = 1;
        else
            vertexAlpha = vertexColor.a;
            
        float projWeight = -projectedUVParams.x * projNoise + (dot(surface.Normal.xyz, texProj) * vertexAlpha - projectedUVParams.w);
            
        if (mesh.Properties.ShaderFlags & ShaderFlags::kHDLODObjects)
            projWeight += (-0.5 + vertexColor.a) * 2.5;

        if (projectedUVParams3.w > 0.5)
        {

        }
        else
        {
            float3 projBaseColor = VanillaDiffuseColor(projectedUVParams2.xyz);
            
            if ((mesh.Properties.ShaderFlags & ShaderFlags::kLODObjects) || (mesh.Properties.ShaderFlags & ShaderFlags::kHDLODObjects))
            {
                projBaseColor = pow(projBaseColor, Features.LODBlending.LODObjectSnowGamma) * Features.LODBlending.LODObjectSnowBrightness;
            }
            
            surface.Albedo = lerp(surface.Albedo, projBaseColor, projWeight > 0 ? 1.0f : 0.0f);
        }
    }
    
    [branch]
    if (mesh.Properties.AlphaFlags != AlphaFlags::None)
    {
        alpha *= material.MaterialAlpha * mesh.Properties.Alpha;
        
        [branch]
        if ((mesh.Properties.ShaderFlags & ShaderFlags::kVertexAlpha) && !(mesh.Properties.ShaderFlags & ShaderFlags::kTreeAnim))
            alpha *= vertexColor.a;

        [branch]
        if (mesh.Properties.AlphaFlags & AlphaFlags::Transmission)
        {
            surface.TransmissionColor = lerp(float3(1.0f, 1.0f, 1.0f), surface.Albedo, alpha);
            surface.Albedo *= alpha;
            surface.Metallic *= alpha;
            surface.SpecTrans = 1.0f;
            surface.IsThinSurface |= (mesh.Properties.ShaderFlags & ShaderFlags::kTwoSided) != 0;
            if (material.Type != Type::TruePBR)
            {
                surface.Roughness = 0.0f;
            }
        }

        [branch]
        if (mesh.Properties.AlphaFlags & AlphaFlags::Additive)
        {
            surface.Albedo = 0.0f;
            surface.Metallic = 0.0f;
            surface.Roughness = 0.0f;
            surface.TransmissionColor = 1.0f;
            surface.SpecTrans = 1.0f;
            surface.F0 = 0.04f;

            surface.SubsurfaceData.HasSubsurface = 0;
            surface.SubsurfaceData.TransmissionColor = 0.0f;
            surface.SubsurfaceData.ScatteringColor = 0.0f;
            surface.SubsurfaceData.Scale = 0.0f;
            surface.SubsurfaceData.Anisotropy = 0.0f;

            surface.CoatColor = 1.0f;
            surface.CoatStrength = 0.0f;
            surface.CoatRoughness = 0.0f;
            surface.CoatF0 = 0.0f;
        }
    }

    [branch]
    if (isWindows)
    {
        surface.TransmissionColor = windowAlpha;
        surface.Albedo *= 1.0f - windowAlpha;
        surface.Emissive *= 0;
        surface.SpecTrans = 1.0f;
    }

    // Hair flowmap processing
#if HAIR_MODE
    [branch]
    if (material.Feature == Feature::kHairTint && HAIRSETTINGS.Enabled) {
        surface.Roughness = 1.0f - saturate(HAIRSETTINGS.HairGlossiness * 0.01f);
        surface.Albedo = saturate(surface.Albedo * HAIRSETTINGS.BaseColorMult);
        [branch]
        if (mesh.Properties.ShaderFlags & ShaderFlags::kBackLighting) {
            Texture2D hairFlowMapTexture = Textures[NonUniformResourceIndex(material.SpecularBackLightingTexture)];
            uint2 hairFlowDimensions;
            hairFlowMapTexture.GetDimensions(hairFlowDimensions.x, hairFlowDimensions.y);
                
            [branch]
            if (hairFlowDimensions.x > 32 && hairFlowDimensions.y > 32) {
                float2 sampledHairFlow2D = hairFlowMapTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).xy;
                    
                [branch]
                if (sampledHairFlow2D.x > 0.0 || sampledHairFlow2D.y > 0.0) {
                    float3 sampledHairFlow = float3(sampledHairFlow2D * 2.0f - 1.0f, 0.0f);
                    float3x3 tbn = float3x3(surface.Tangent, surface.Bitangent, surface.Normal);
                    float3 hairRootDirection = normalize(mul(sampledHairFlow, tbn));
                        
                    // Re-orthogonalize T and B to N and the new hair root direction
                    hairRootDirection = normalize(hairRootDirection - surface.Normal * dot(hairRootDirection, surface.Normal));
                    surface.Bitangent = hairRootDirection;
                        
                    float hairHandedness = (dot(cross(surface.Normal, surface.Tangent), surface.Bitangent) < 0.0f) ? -1.0f : 1.0f;
                    surface.Tangent = normalize(cross(surface.Bitangent, surface.Normal)) * hairHandedness;
                }
            }
        }
    }
#endif

    // ---- Wetness Effects ----
    // Apply wetness to non-water, non-eye materials
    if (material.Type != Type::Water && material.Feature != Feature::kEye)
    {
        bool isSkinned = (material.Feature == Feature::kFaceGen || material.Feature == Feature::kHairTint);
        Wetness::WetnessParams wetnessParams = Wetness::ComputeWetness(
            surface.Position,
            normalWS,
            surface.Normal,
            Camera.Position,
            Camera.WaterData,
            Features.WetnessEffects,
            isSkinned,
            surface.Primary);
        Wetness::ApplyWetness(surface, wetnessParams);
    }
}

void EffectMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, in Mesh mesh)
{
    EffectMaterialData effect = Materials[0].Load<EffectMaterialData>(mesh.GetMaterialOffset());
    const float mipLevel = surface.MipLevel;
    
    Texture2D baseTexture = Textures[NonUniformResourceIndex(effect.SourceTexture)];

    float4 baseTexColor = baseTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
    baseTexColor.xyz = baseTexColor.xyz;
    
    float4 baseColorMul = effect.BaseColor;       
    baseColorMul.xyz = baseColorMul.xyz;
    
    [branch]
    if ((mesh.Properties.ShaderFlags & ShaderFlags::kVertexColors) && !(mesh.Properties.ShaderFlags & ShaderFlags::kProjectedUV))
    {
        baseColorMul *= float4(vertexColor.xyz, vertexColor.w);
    }

    float4 baseColor = float4(1, 1, 1, 1);
    float baseColorScale = effect.BaseColorScale;

    [branch]
    if (mesh.Properties.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
    {
        Texture2D effectTexture = Textures[NonUniformResourceIndex(effect.EffectTexture)];

        float2 grayscaleToColorUv = float2(baseTexColor.y, baseColorMul.x);

        baseColor.xyz = baseColorScale * effectTexture.SampleLevel(ClampSampler, grayscaleToColorUv, mipLevel).xyz;
    }
    else
    {
        baseColor = baseTexColor * baseColorMul;
    }

    surface.Albedo = 0;
    surface.Emissive = EffectToLinear(baseColor.xyz) * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Effect);
}

void WaterMaterial(inout Surface surface, in float2 texCoord0, in float3 tangentWS, in float3 bitangentWS, in Mesh mesh)
{
    WaterMaterialData water = Materials[0].Load<WaterMaterialData>(mesh.GetMaterialOffset());
    const float mipLevel = surface.MipLevel;

    surface.Albedo = float3(1.0f, 1.0f, 1.0f);
    surface.Roughness = 0.0f;
    surface.Metallic = 0.0f;
    surface.F0 = 0.02f;
    surface.IOR = 1.33f;
 
    const bool hasFlowMap = (mesh.Properties.ShaderFlags & WaterShaderFlags::kEnableFlowmap) != 0;
    const bool hasBlendNormals = true; // Should come from WaterShaderFlags::kBlendNormals but it is always false for some reason
    const bool hasNormalTexcoord = (mesh.Properties.ShaderFlags & WaterShaderFlags::kVertexUV) != 0;
    
    const bool hasWading = false;
    
    const bool hasVertexColor = false;
    
    const float scale = 0.001f;
    
    float2 normalScroll1 = water.NormalScrolls.xy;
    float2 normalScroll2 = water.NormalScrolls.zw;
    float2 normalScroll3 = water.NormalScroll3AndScale.xy;
    
    float3 normalsScale = float3(water.NormalScroll3AndScale.z, water.NormalScroll3AndScale.w, water.UVScaleAndObjectUV.x);
    
    float3 objectUV = water.UVScaleAndObjectUV.yzw;

    float4 cellTexCoordOffset = water.CellTexCoordOffset;
    
    float2 scrollAdjust1;
    float2 scrollAdjust2;
    float2 scrollAdjust3;
    
    if (hasNormalTexcoord && !hasFlowMap)
    {
        float3 scaledNormals = normalsScale * scale;

        scrollAdjust1 = texCoord0.xy / scaledNormals.xx;
        scrollAdjust2 = texCoord0.xy / scaledNormals.yy;
        scrollAdjust3 = texCoord0.xy / scaledNormals.zz;
    } else
    {
        scrollAdjust1 = surface.Position.xy / normalsScale.xx;
        scrollAdjust2 = surface.Position.xy / normalsScale.yy;
        scrollAdjust3 = surface.Position.xy / normalsScale.zz;        
    }

    if (hasFlowMap)
    {
        float4 flowCoord = float4(0.0f, 0.0f, 0.0f, 0.0f);

        if (hasWading)
        {
            flowCoord.xy =
                ((-0.5 + texCoord0.xy) * 0.1 + cellTexCoordOffset.xy) +
                float2(cellTexCoordOffset.z,
                       -cellTexCoordOffset.w + objectUV.x) / objectUV.xx;

            flowCoord.zw = -0.25 + (texCoord0.xy * 0.5 + objectUV.yz);
        }
        else
        {
            flowCoord.xy = (cellTexCoordOffset.xy + texCoord0.xy) / objectUV.xx;
            flowCoord.zw = (cellTexCoordOffset.zw + texCoord0.xy);
        }

        // ReflectionColor.w
        const float flowScroll = Camera.Time;
        
        const float2 flowmapDimensions = objectUV.xx;
        float2 uvShift = 1 / (128 * flowmapDimensions);
        
        float2 normalMul = 0.5 + -(-0.5 + abs(frac(flowCoord.xy * (64 * flowmapDimensions)) * 2 - 1));
        
        Texture2D normals04Texture = Textures[NonUniformResourceIndex(water.FlowmapTexture)];
        
        float3 normals1 = GetFlowmapNormal(WaterFlowMap, PointWrapSampler, normals04Texture, DefaultSampler, flowCoord, uvShift, 9.92, 0, flowScroll, mipLevel);
        float3 normals2 = GetFlowmapNormal(WaterFlowMap, PointWrapSampler, normals04Texture, DefaultSampler, flowCoord, float2(0, uvShift.y), 10.64, 0.27, flowScroll, mipLevel);
        float3 normals3 = GetFlowmapNormal(WaterFlowMap, PointWrapSampler, normals04Texture, DefaultSampler, flowCoord, float2(0, 0), 8, 0, flowScroll, mipLevel);
        float3 normals4 = GetFlowmapNormal(WaterFlowMap, PointWrapSampler, normals04Texture, DefaultSampler, flowCoord, float2(uvShift.x, 0), 8.48, 0.62, flowScroll, mipLevel);
        
        float2 flowmapNormalWeighted =
		    normalMul.y * (normalMul.x * normals3.xy + (1 - normalMul.x) * normals4.xy) +
		    (1 - normalMul.y) *
			    (normalMul.x * normals2.xy + (1 - normalMul.x) * normals1.xy);
        
        float2 flowmapDenominator = sqrt(normalMul * normalMul + (1 - normalMul) * (1 - normalMul));
        
        float3 flowmapNormal = float3(((-0.5 + flowmapNormalWeighted) / (flowmapDenominator.x * flowmapDenominator.y)), 0);     
        flowmapNormal.z = sqrt(1 - flowmapNormal.x * flowmapNormal.x - flowmapNormal.y * flowmapNormal.y);
        
        surface.Normal = normalize(flowmapNormal);
    } else
    {
        float2 normalCoord1 = normalScroll1 + scrollAdjust1;
        Texture2D normals01Texture = Textures[NonUniformResourceIndex(water.NormalsTexture0)];
        float3 normals1 = normals01Texture.SampleLevel(DefaultSampler, normalCoord1, mipLevel).xyz * 2.0 + float3(-1, -1, -2);
        
        if (hasBlendNormals)
        {
            float2 normalCoord2 = normalScroll2 + scrollAdjust2;
            float2 normalCoord3 = normalScroll3 + scrollAdjust3;
        
            Texture2D normals02Texture = Textures[NonUniformResourceIndex(water.NormalsTexture1)];
            Texture2D normals03Texture = Textures[NonUniformResourceIndex(water.NormalsTexture2)];
    
            float3 normals2 = normals02Texture.SampleLevel(DefaultSampler, normalCoord2, mipLevel).xyz * 2.0 - 1.0;
            float3 normals3 = normals03Texture.SampleLevel(DefaultSampler, normalCoord3, mipLevel).xyz * 2.0 - 1.0;
        
            surface.Normal = normalize(
                float3(0, 0, 1) +
                water.Amplitude0 * normals1 +
                water.Amplitude1 * normals2 +
                water.Amplitude2 * normals3
            );
        }
        else
        {
            surface.Normal = normalize(
                float3(0, 0, 1) + normals1
            );
        }
    }

    // ---- Rain ripples on water surface ----
    if (surface.Primary && Features.WetnessEffects.Raining > 0.0 && Features.WetnessEffects.EnableRaindropFx)
    {
        float3 ripplePosition = surface.Position.xyz;
        float4 raindropInfo = Wetness::GetRainDrops(
            ripplePosition,
            Features.WetnessEffects.Time,
            float3(0, 0, 1),  // water geometric normal (flat, z-up)
            1.0,              // full ripple strength on water
            Features.WetnessEffects);
        float3 rippleNormal = normalize(raindropInfo.xyz);
        surface.Normal = ReorientNormal(rippleNormal, surface.Normal);
    }

    surface.Tangent = normalize(tangentWS - surface.Normal * dot(tangentWS, surface.Normal));
    surface.Bitangent = cross(surface.Normal, surface.Tangent);
    surface.Bitangent *= (dot(surface.Bitangent, bitangentWS) < 0.0f) ? -1.0f : 1.0f;
    
    // Distance-based absorption via Beer-Lambert law instead of flat surface tint.
    // The absorption coefficient is derived from the game's water color at a reference depth.
    static const float WATER_ABSORPTION_REFERENCE_DEPTH = 600.0;
    float3 waterColor = saturate(water.ShallowColor.rgb);
    surface.VolumeAbsorption = -log(max(waterColor, 1e-4)) / WATER_ABSORPTION_REFERENCE_DEPTH * Raytracing.WaterAbsorptionScale;
    surface.TransmissionColor = float3(1.0f, 1.0f, 1.0f);
    surface.SpecTrans = 1.0f;
}

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
        diffTex0  = pbrLand.BaseColorTexture0;  diffTex1  = pbrLand.BaseColorTexture1;
        diffTex2  = pbrLand.BaseColorTexture2;  diffTex3  = pbrLand.BaseColorTexture3;
        diffTex4  = pbrLand.BaseColorTexture4;  diffTex5  = pbrLand.BaseColorTexture5;
        normTex0  = pbrLand.NormalTexture0;     normTex1  = pbrLand.NormalTexture1;
        normTex2  = pbrLand.NormalTexture2;     normTex3  = pbrLand.NormalTexture3;
        normTex4  = pbrLand.NormalTexture4;     normTex5  = pbrLand.NormalTexture5;
        rmaosTex0 = pbrLand.RMAOSTexture0;      rmaosTex1 = pbrLand.RMAOSTexture1;
        rmaosTex2 = pbrLand.RMAOSTexture2;      rmaosTex3 = pbrLand.RMAOSTexture3;
        rmaosTex4 = pbrLand.RMAOSTexture4;      rmaosTex5 = pbrLand.RMAOSTexture5;
        overlayTex = pbrLand.OverlayTexture;
        noiseTex   = pbrLand.NoiseTexture;
        roughness0 = pbrLand.RoughnessScale0; roughness1 = pbrLand.RoughnessScale1;
        roughness2 = pbrLand.RoughnessScale2; roughness3 = pbrLand.RoughnessScale3;
        roughness4 = pbrLand.RoughnessScale4; roughness5 = pbrLand.RoughnessScale5;
        specular0  = pbrLand.SpecularLevel0;  specular1  = pbrLand.SpecularLevel1;
        specular2  = pbrLand.SpecularLevel2;  specular3  = pbrLand.SpecularLevel3;
        specular4  = pbrLand.SpecularLevel4;  specular5  = pbrLand.SpecularLevel5;
        pbrFlags = pbrLand.PBRFlags;
    }
    else
    {
        LandscapeMaterialDataExtra land = Materials[0].Load<LandscapeMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
        diffTex0  = material.DiffuseTexture;  diffTex1  = land.DiffuseTexture1;
        diffTex2  = land.DiffuseTexture2;     diffTex3  = land.DiffuseTexture3;
        diffTex4  = land.DiffuseTexture4;     diffTex5  = land.DiffuseTexture5;
        normTex0  = material.NormalTexture;   normTex1  = land.NormalTexture1;
        normTex2  = land.NormalTexture2;      normTex3  = land.NormalTexture3;
        normTex4  = land.NormalTexture4;      normTex5  = land.NormalTexture5;
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
        rmaos += BlendLandTexture(rmaosTex0, texCoord0, landBlend0.x, mipLevel) * float4(roughness0, 1.0f, 1.0, specular0);
        rmaos += BlendLandTexture(rmaosTex1, texCoord0, landBlend0.y, mipLevel) * float4(roughness1, 1.0f, 1.0, specular1);
        rmaos += BlendLandTexture(rmaosTex2, texCoord0, landBlend0.z, mipLevel) * float4(roughness2, 1.0f, 1.0, specular2);
        rmaos += BlendLandTexture(rmaosTex3, texCoord0, landBlend0.w, mipLevel) * float4(roughness3, 1.0f, 1.0, specular3);
        rmaos += BlendLandTexture(rmaosTex4, texCoord0, landBlend1.x, mipLevel) * float4(roughness4, 1.0f, 1.0, specular4);
        rmaos += BlendLandTexture(rmaosTex5, texCoord0, landBlend1.y, mipLevel) * float4(roughness5, 1.0f, 1.0, specular5);
        
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

void DistantTreeMaterial(inout Surface surface, in float2 texCoord0, in Mesh mesh)
{
    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.DiffuseTexture)];
    float4 diffuse = baseTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);
    float alpha = diffuse.a * mesh.Properties.Alpha;

    surface.Albedo = diffuse.rgb;
}

void GrassMaterial(inout Surface surface, in float2 texCoord0, in Mesh mesh)
{
    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.DiffuseTexture)];
    float4 diffuse = baseTexture.SampleLevel(DefaultSampler, texCoord0, surface.MipLevel);
    float alpha = diffuse.a * mesh.Properties.Alpha;

    surface.Albedo = VanillaDiffuseColor(diffuse.rgb);
}

#endif // SURFACE_SKYRIM_HLSL