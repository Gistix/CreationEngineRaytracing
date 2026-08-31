#ifndef LIGHTING_MATERIAL_FUNC_HLSL
#define LIGHTING_MATERIAL_FUNC_HLSL

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
#include "interop/Material/Skyrim/ParallaxMaterialData.hlsli"
#include "interop/Material/Skyrim/ParallaxOccMaterialData.hlsli"
#include "include/Wetness.hlsli"
#include "include/Common/Triplanar.hlsli"
#include "include/Common/ExtendedMaterials.hlsli"

#include "include/Material/Common.hlsli"
#include "include/Material/Skyrim/Common.hlsli"

void LightingMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, in float3 normalWS, in float3 tangentWS, in float3 bitangentWS, in Mesh mesh, Properties props, float4 boneRotation, float3 viewDir, float dist)
{
    LightingMaterialData material = Materials[0].Load<LightingMaterialData>(mesh.GetMaterialOffset());
    float mipLevel = surface.Geometry.MipLevel;

    const Texture2D baseTexture = Textures[NonUniformResourceIndex(material.DiffuseTexture)];

    const bool clampSampler = props.ShaderFlags & ShaderFlags::kLODLandscape;

    const Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
    
    const bool skinEnabled = (material.Type == Type::Lighting) &&
        (material.Feature == Feature::kFaceGen || material.Feature == Feature::kSkinTint) &&
        !(props.AlphaFlags & AlphaFlags::Additive) &&
        SKINSETTINGS.skinParams.w > 0.0f;
    
    const bool isTruePBR = material.Type == Type::TruePBR;
    
    const bool enabledParallax = Features.ExtendedMaterial.EnableParallax;
    
    const bool isVanillaParallax = enabledParallax && !isTruePBR && material.Feature == Feature::kParallax;
    const bool isVanillaParallaxOcc = enabledParallax && !isTruePBR && material.Feature == Feature::kParallaxOcc;
    
    bool isPBRParallax = false;
 
    PBRMaterialData pbr;
    if (isTruePBR)
    {
        pbr = Materials[0].Load<PBRMaterialData>(mesh.GetMaterialOffset());
        
        isPBRParallax = enabledParallax && (pbr.PBRFlags & PBR::Flags::HasDisplacement) != 0;
    }
    
    // Parallax
    if (isPBRParallax || isVanillaParallax || isVanillaParallaxOcc)
    {
        uint16_t displacementTextureIdx = 0;
        float3x3 tbnTr = float3x3(tangentWS, bitangentWS, normalWS);
        float noise = 0;
        float pixelOffset;
        
        DisplacementParams displacementParams;
        displacementParams.DisplacementScale = 1.f;
        displacementParams.DisplacementOffset = 0.f;
        displacementParams.HeightScale = 1;
        displacementParams.FlattenAmount = 0;
        
        bool interlayer = false;
        
        [branch]
        if (isPBRParallax)
        {
            displacementTextureIdx = pbr.DisplacementTexture;
            displacementParams.HeightScale *= pbr.DisplacementScale;
                    
            interlayer = (pbr.PBRFlags & PBR::Flags::InterlayerParallax) != 0;
        }
        else if (isVanillaParallax || isVanillaParallaxOcc)
        {
            // Load ParallaxOcc for Parallax as well
            ParallaxOccMaterialData parallaxOccMaterial = Materials[0].Load<ParallaxOccMaterialData>(mesh.GetMaterialOffset());
           
            displacementTextureIdx = parallaxOccMaterial.HeightTexture;

            // Only valid if material is ParallaxOcc
            if (isVanillaParallaxOcc)
                displacementParams.HeightScale *= parallaxOccMaterial.Scale;
        }
        
        Texture2D displacementTexture = Textures[NonUniformResourceIndex(displacementTextureIdx)];
        texCoord0 = ExtendedMaterials::GetParallaxCoords(dist, texCoord0, mipLevel, viewDir, tbnTr, noise, displacementTexture, DefaultSampler, 0, displacementParams, interlayer, pixelOffset);
    }
    
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

    if (props.ShaderFlags & ShaderFlags::kModelSpaceNormals)
    {
        // Swizzle matches vanilla shaders        
        normal = normalize(normal.xzy * 2.0f - 1.0f);
        
        if (mesh.Type == MeshType::Skinned || mesh.Type == MeshType::Dynamic)
        {
            surface.Geometry.Normal = RotateByQuaternion(normal, boneRotation);
            CreateOrthonormalBasis(surface.Geometry.Normal, surface.Frame.Tangent, surface.Frame.Bitangent);
        }
        else
        {
            surface.Geometry.Normal = normal;
        }
        
        // Use shading values since the geometry ones aren't available
        surface.Geometry.GeomNormal = surface.Geometry.Normal;
        surface.Frame.GeomTangent = surface.Frame.Tangent;
    }
    else
    {
        NormalMap(
            normal,
            normalWS, tangentWS, bitangentWS,
            surface.Geometry.Normal, surface.Frame.Tangent, surface.Frame.Bitangent
        );
    }

    vertexColor.rgb = saturate(vertexColor.rgb / max(max(vertexColor.r, vertexColor.g), vertexColor.b));
    
    const bool isWindows = material.Feature == Feature::kGlowMap && props.ShaderFlags & ShaderFlags::kAssumeShadowmask;
    float3 windowAlpha = float3(0.0f, 0.0f, 0.0f);

    float alpha = 1.0f;
    
    [branch]
    if (material.Type == Type::TruePBR)
    {
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

        surface.Material.Albedo = albedo.rgb * vertexColor.rgb;
        surface.Material.Emissive = emissive * EmitColorToLinear(props.EmissiveColor.rgb) * props.EmissiveColor.a * EmitColorMult() * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        surface.Material.Roughness = saturate(rmaos.x * pbr.RoughnessScale);
        surface.Material.Metallic = saturate(rmaos.y);
        surface.Material.AO = rmaos.z;
        surface.Material.F0 = pbr.SpecularLevel * rmaos.w;

        if (pbr.PBRFlags & PBR::Flags::Subsurface)
        {
            Texture2D subsurfaceTexture = Textures[NonUniformResourceIndex(pbr.FeaturesTexture0)];

            float4 subsurfaceColor = subsurfaceTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
            float thickness = subsurfaceColor.a * pbr.FeatureScalar;
            surface.Material.TransmissionColor = surface.Material.Albedo;
            surface.Material.DiffTrans = 0.5f;

            if (!(props.ShaderFlags & ShaderFlags::kTwoSided))
            {
                surface.SubsurfaceData.ScatteringColor = subsurfaceColor.rgb * pbr.FeatureColor.rgb;
                surface.SubsurfaceData.TransmissionColor = surface.Material.Albedo;

                surface.SubsurfaceData.Scale = 40.0f;
                surface.SubsurfaceData.Anisotropy = 0.0f;

                surface.SubsurfaceData.HasSubsurface = any(surface.SubsurfaceData.ScatteringColor) > 0.0f ? 1 : 0;
            }
        }

        // Coat (TwoLayer)
        if (pbr.PBRFlags & PBR::Flags::TwoLayer)
        {
            half4 coatColorParam = pbr.FeatureColor;
            surface.Coat.Color = coatColorParam.rgb;
            surface.Coat.Strength = coatColorParam.a;
            surface.Coat.Roughness = pbr.FeatureScalar;
            surface.Coat.F0 = float3(0.04, 0.04, 0.04);

            if (pbr.PBRFlags & PBR::Flags::HasFeatureTexture0)
            {
                Texture2D coatColorTexture = Textures[NonUniformResourceIndex(pbr.FeaturesTexture0)];
                float4 sampledCoat = coatColorTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                surface.Coat.Color *= sampledCoat.rgb;
                surface.Coat.Strength *= sampledCoat.a;
            }

            if (pbr.PBRFlags & PBR::Flags::HasFeatureTexture1)
            {
                Texture2D coatNormalTexture = Textures[NonUniformResourceIndex(pbr.FeaturesTexture1)];
                float4 sampledCoatNormal = coatNormalTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                surface.Coat.Roughness *= sampledCoatNormal.a;

                if (pbr.PBRFlags & PBR::Flags::CoatNormal)
                {
                    NormalMap(
                        sampledCoatNormal.xyz,
                        normalWS, tangentWS, bitangentWS,
                        surface.Coat.Normal, surface.Coat.Tangent, surface.Coat.Bitangent
                    );
                }
            }
        }

        // Fuzz (OpenPBR §3.7)
        if (pbr.PBRFlags & PBR::Flags::Fuzz)
        {
            half4 fuzzColorWeight = pbr.FeatureColor;
            surface.Fuzz.Color = fuzzColorWeight.rgb;
            surface.Fuzz.Weight = fuzzColorWeight.a;

            if (pbr.PBRFlags & PBR::Flags::HasFeatureTexture1)
            {
                Texture2D fuzzTexture = Textures[NonUniformResourceIndex(pbr.FeaturesTexture1)];
                float4 sampledFuzz = fuzzTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                surface.Fuzz.Color *= sampledFuzz.rgb;
                surface.Fuzz.Weight *= sampledFuzz.a;
            }
        }

        // Glint (Discrete Stochastic Microfacet Model)
#if defined(GLINT)
        if (pbr.PBRFlags & PBR::Flags::Glint)
        {
            half4 glintParams = pbr.GlintParameters;
            surface.Glint.ScreenSpaceScale = glintParams.x;
            surface.Glint.LogMicrofacetDensity = glintParams.y;
            surface.Glint.MicrofacetRoughness = glintParams.z;
            surface.Glint.DensityRandomization = glintParams.w;
            surface.Glint.TexCoord = texCoord0;
        }
#endif

        // OpenPBR 3.11: Emission sits below the coat and is absorbed.
        // At normal incidence, coat_color = T^2 gives the round-trip absorption.
        if (surface.Coat.Strength > 0)
        {
            surface.Material.Emissive *= lerp(float3(1, 1, 1), surface.Coat.Color, surface.Coat.Strength);
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
        if (props.ShaderFlags & ShaderFlags::kLODLandscape)
        {
            albedo = pow(albedo, Features.LODBlending.LODTerrainGamma) * Features.LODBlending.LODTerrainBrightness;

        }
        else if ((props.ShaderFlags & ShaderFlags::kLODObjects) || (props.ShaderFlags & ShaderFlags::kHDLODObjects))
        {
            albedo = pow(albedo, Features.LODBlending.LODObjectGamma) * Features.LODBlending.LODObjectBrightness;
        }
        
        surface.Material.Albedo = VanillaDiffuseColor(albedo);

        [branch]
        if (material.Feature == Feature::kHairTint)
        {
            HairTintMaterialDataExtra hair = Materials[0].Load<HairTintMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            surface.Material.Albedo *= VanillaDiffuseColor(hair.TintColor);
        }
    
        [branch]
        if (props.ShaderFlags & ShaderFlags::kSpecular)
        {
            float3 specularColor = material.SpecularColor;
            float specularStrength = 0;
            
            [branch]
            if (props.ShaderFlags & ShaderFlags::kModelSpaceNormals)
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

            surface.Material.Roughness = lerp(roughnessFromSpecularTexture, roughnessFromShininess, specularStrength);
            surface.Material.F0 = clamp(0.08f * specularColor * material.SpecularColorScale, 0.02f, 0.08f);
        }
         
        [branch]
        if (props.ShaderFlags & ShaderFlags::kEnvMap || props.ShaderFlags & ShaderFlags::kEyeReflect)
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
                surface.Material.Roughness = 1.0f - envMask.y;
                surface.Material.Metallic = envMask.z;
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
                        surface.Material.F0 = lerp(surface.Material.F0, ColorToLinear(envColorBase.rgb), envMask.r);
                        surface.Material.Roughness = lerp(surface.Material.Roughness, envColorBase.a, envMask.r);
                    }
                    else
                    {
                        surface.Material.F0 = lerp(surface.Material.F0, float3(1.0, 1.0, 1.0), envMask.r);
                        surface.Material.Roughness = lerp(surface.Material.Roughness, 1.0 / 7.0, envMask.r);
                    }
                }
                else
                {
                    // Static cubemap: use +X face average color as metallic tint
                    float3 faceAvg = envCubemap.SampleLevel(DefaultSampler, float3(1.0, 0.0, 0.0), 15).rgb;
                    surface.Material.F0 = lerp(surface.Material.F0, saturate(ColorToLinear(faceAvg)), envMask.r);
                    surface.Material.Roughness = lerp(surface.Material.Roughness, 0.0f, envMask.r);
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
            surface.Material.Emissive = GlowToLinear(glow) * EmitColorToLinear(props.EmissiveColor.rgb) * props.EmissiveColor.a * EmitColorMult() * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        }
        else
        {
            surface.Material.Emissive = surface.Material.Albedo * EmitColorToLinear(props.EmissiveColor.rgb) * props.EmissiveColor.a * EmitColorMult() * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        }

        [branch]
        if (material.Feature == Feature::kFaceGen)
        {
            FacegenMaterialDataExtra facegen = Materials[0].Load<FacegenMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            float3 gammaAlbedo = VanillaDiffuseColorGamma(surface.Material.Albedo);
            
            Texture2D detailTexture = Textures[NonUniformResourceIndex(facegen.DetailTexture)];
            float3 detailColor = detailTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;
            detailColor = float3(3.984375, 3.984375, 3.984375) * (float3(0.00392156886, 0, 0.00392156886) + detailColor);
               
            Texture2D tintTexture = Textures[NonUniformResourceIndex(facegen.TintTexture)];
            float3 tintColor = tintTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;
            tintColor = tintColor * gammaAlbedo * 2.0f;
            tintColor = tintColor - tintColor * gammaAlbedo;
            surface.Material.Albedo = VanillaDiffuseColor((gammaAlbedo * gammaAlbedo + tintColor) * detailColor);
                
        }
        else if (material.Feature == Feature::kSkinTint)
        {
            FacegenTintMaterialDataExtra tintData = Materials[0].Load<FacegenTintMaterialDataExtra>(mesh.GetMaterialOffset() + kLightingSize);
            float3 gammaAlbedo = VanillaDiffuseColorGamma(surface.Material.Albedo);
            
            float3 tintColor = tintData.TintColor * gammaAlbedo * 2.0f;
            tintColor = tintColor - tintColor * gammaAlbedo;
            surface.Material.Albedo = VanillaDiffuseColor(float3(1.01171875f, 0.99609375f, 1.01171875f) * (gammaAlbedo * gammaAlbedo + tintColor));
        }
        
        [branch]
        if (material.Feature == Feature::kFaceGen || material.Feature == Feature::kSkinTint)
        {
            surface.Material.F0 = 0.02776f;
            surface.Material.Metallic = 0.0f;
            surface.SubsurfaceData.HasSubsurface = 1;
            surface.SubsurfaceData.Anisotropy = -0.5f;

            // Typical skin values
            surface.SubsurfaceData.ScatteringColor = float3(4.820f, 1.690f, 1.090f);
            surface.SubsurfaceData.TransmissionColor = surface.Material.Albedo;
            surface.SubsurfaceData.Scale = 1.f;

            if (skinEnabled)
            {
                Texture2D rfaosTexture = Textures[0]; // TODO: RFAOSTexture — CS skin feature not yet in typed struct
                uint2 rfaosDimensions;
                rfaosTexture.GetDimensions(rfaosDimensions.x, rfaosDimensions.y);
                bool hasValidRFAOS = rfaosDimensions.x > 32 && rfaosDimensions.y > 32;

                surface.Material.Albedo *= SKINSETTINGS.skinParams2.w;
                surface.Material.Roughness = SKINSETTINGS.skinParams.x;
                surface.Material.F0 = SKINSETTINGS.skinParams2.zzz;

                // Skin coat layer (second specular lobe)
                surface.Coat.Strength = SKINSETTINGS.skinParams2.x;
                surface.Coat.Roughness = SKINSETTINGS.skinParams.y;
                surface.Coat.F0 = float3(0.04, 0.04, 0.04);
                surface.Coat.Normal = surface.Geometry.Normal;

                if (hasValidRFAOS)
                {
                    float4 rfaos = rfaosTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                    surface.Material.Roughness = rfaos.x * SKINSETTINGS.physicalParams.x;
                    surface.Coat.Roughness = rfaos.x * SKINSETTINGS.physicalParams.y;
                    surface.Material.F0 = 0.08 * rfaos.w * SKINSETTINGS.physicalParams.z;
                    surface.Material.AO = rfaos.z;
                }
            }
            else
            {
                surface.Material.Roughness *= K_2_PI;
            }
        }
        else if (material.Feature == Feature::kEye)
        {
            surface.Material.Roughness = 0.2f;
            surface.Material.F0 = 0.02776f;
            surface.Material.Metallic = 0.0f;
            surface.SubsurfaceData.HasSubsurface = 1;
            surface.SubsurfaceData.Anisotropy = -0.5f;
            
            // Typical eye values
            surface.SubsurfaceData.ScatteringColor = float3(0.482f, 0.169f, 0.109f);
            surface.SubsurfaceData.TransmissionColor = surface.Material.Albedo;
            surface.SubsurfaceData.Scale = 10.f;

            surface.Coat.Strength = 1.f;
            surface.Coat.Roughness = 0.0f;
            surface.Coat.F0 = 0.026f;
        }
        else if (props.ShaderFlags & ShaderFlags::kSoftLighting || props.ShaderFlags & ShaderFlags::kBackLighting)
        {
            surface.Material.TransmissionColor = surface.Material.Albedo;
            surface.Material.DiffTrans = 0.5f;
            
            if (!(props.ShaderFlags & ShaderFlags::kTwoSided) && (props.ShaderFlags & ShaderFlags::kSoftLighting))
            {
                surface.SubsurfaceData.HasSubsurface = 1;
                surface.SubsurfaceData.Anisotropy = -0.5f;

                Texture2D scatterTexture = Textures[NonUniformResourceIndex(material.RimSoftLightingTexture)];
                surface.SubsurfaceData.ScatteringColor = scatterTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb * K_PI;
                surface.SubsurfaceData.TransmissionColor = surface.Material.Albedo;
                surface.SubsurfaceData.Scale = 1.f;
            }
        }

        [branch]
        if (props.ShaderFlags & ShaderFlags::kRefraction) // As glass
        {
            surface.Material.Albedo = float3(0.0f, 0.0f, 0.0f);
            surface.Material.Roughness = 0.0f;
            surface.Material.Emissive = float3(0.0f, 0.0f, 0.0f);
            surface.Material.F0 = 0.04f;
            surface.Material.Metallic = 0.0f;
            surface.Material.TransmissionColor = 1.0f;
            surface.Material.SpecTrans = 1.0f;
            surface.Material.IsThinSurface = true;
            alpha = 0.0f;
        }
    }
    else
    {
        surface.Material.Albedo = float3(1.0f, 0.0f, 1.0f);
    }

    [branch]
    if (props.ShaderFlags & ShaderFlags::kProjectedUV)
    {
        const float4 projectedUVParams = props.ProjectedUVParams;
        const float4 projectedUVParams2 = props.ProjectedUVParams2;
        const float4 projectedUVParams3 = props.ProjectedUVParams3;
            
        float3 triWeights = Triplanar::GetWeights(surface.Geometry.GeomNormal, surface.Geometry.FaceNormal);
        float projNoise = Triplanar::Sample(ProjNoiseMap, DefaultSampler, mipLevel, surface.Geometry.Position, triWeights, projectedUVParams.z).x;

        float3 texProj = props.TextureProj.xyz;
             
        float vertexAlpha;
        if ((props.ShaderFlags & ShaderFlags::kTreeAnim) || (props.ShaderFlags & ShaderFlags::kHDLODObjects))
            vertexAlpha = 1;
        else
            vertexAlpha = vertexColor.a;
            
        float projWeight = -projectedUVParams.x * projNoise + (dot(surface.Geometry.Normal.xyz, texProj) * vertexAlpha - projectedUVParams.w);
            
        if (props.ShaderFlags & ShaderFlags::kHDLODObjects)
            projWeight += (-0.5 + vertexColor.a) * 2.5;

        if (projectedUVParams3.w > 0.5)
        {

        }
        else
        {
            float3 projBaseColor = VanillaDiffuseColor(projectedUVParams2.xyz);
            
            if ((props.ShaderFlags & ShaderFlags::kLODObjects) || (props.ShaderFlags & ShaderFlags::kHDLODObjects))
            {
                projBaseColor = pow(projBaseColor, Features.LODBlending.LODObjectSnowGamma) * Features.LODBlending.LODObjectSnowBrightness;
            }
            
            surface.Material.Albedo = lerp(surface.Material.Albedo, projBaseColor, projWeight > 0 ? 1.0f : 0.0f);
        }
    }
    
    MaterialAlpha(props, alpha, material.MaterialAlpha, vertexColor.a, surface);

    [branch]
    if (isWindows)
    {
        surface.Material.TransmissionColor = windowAlpha;
        surface.Material.Albedo *= 1.0f - windowAlpha;
        surface.Material.Emissive *= 0;
        surface.Material.SpecTrans = 1.0f;
    }

    // Hair flowmap processing
#if HAIR_MODE
    [branch]
    if (material.Feature == Feature::kHairTint && HAIRSETTINGS.Enabled) {
        surface.Material.Roughness = 1.0f - saturate(HAIRSETTINGS.HairGlossiness * 0.01f);
        surface.Material.Albedo = saturate(surface.Material.Albedo * HAIRSETTINGS.BaseColorMult);
        [branch]
        if (props.ShaderFlags & ShaderFlags::kBackLighting) {
            Texture2D hairFlowMapTexture = Textures[NonUniformResourceIndex(material.SpecularBackLightingTexture)];
            uint2 hairFlowDimensions;
            hairFlowMapTexture.GetDimensions(hairFlowDimensions.x, hairFlowDimensions.y);
                
            [branch]
            if (hairFlowDimensions.x > 32 && hairFlowDimensions.y > 32) {
                float2 sampledHairFlow2D = hairFlowMapTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).xy;
                    
                [branch]
                if (sampledHairFlow2D.x > 0.0 || sampledHairFlow2D.y > 0.0) {
                    float3 sampledHairFlow = float3(sampledHairFlow2D * 2.0f - 1.0f, 0.0f);
                    float3x3 tbn = float3x3(surface.Frame.Tangent, surface.Frame.Bitangent, surface.Geometry.Normal);
                    float3 hairRootDirection = normalize(mul(sampledHairFlow, tbn));
                        
                    // Re-orthogonalize T and B to N and the new hair root direction
                    hairRootDirection = normalize(hairRootDirection - surface.Geometry.Normal * dot(hairRootDirection, surface.Geometry.Normal));
                    surface.Frame.Bitangent = hairRootDirection;
                        
                    float hairHandedness = (dot(cross(surface.Geometry.Normal, surface.Frame.Tangent), surface.Frame.Bitangent) < 0.0f) ? -1.0f : 1.0f;
                    surface.Frame.Tangent = normalize(cross(surface.Frame.Bitangent, surface.Geometry.Normal)) * hairHandedness;
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
            surface.Geometry.Position,
            normalWS,
            surface.Geometry.Normal,
            Camera.Position,
            Camera.WaterData,
            Features.WetnessEffects,
            isSkinned,
            surface.Primary);
        Wetness::ApplyWetness(surface, wetnessParams);
    }
}

#endif // LIGHTING_MATERIAL_FUNC_HLSL
