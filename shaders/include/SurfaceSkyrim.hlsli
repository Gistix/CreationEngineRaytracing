#ifndef SURFACE_SKYRIM_HLSL
#define SURFACE_SKYRIM_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"

#include "include/Surface.hlsli"

#include "include/Utils/VanillaToPBR.hlsli"

#include "interop/Material.hlsli"

#define LIGHTINGSETTINGS Raytracing
#define HAIRSETTINGS Features.HairSpecular

void DefaultMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, in float3 normalWS, in float3 tangentWS, in float3 bitangentWS, in float handedness, in Material material)
{
    float mipLevel = surface.MipLevel;

#if defined(DEBUG_SHADERTYPE)
    [branch]
    if (material.ShaderType == ShaderType::TruePBR) {
        Albedo = float3(1.0f, 0.0f, 0.0f);
    } else if (material.ShaderType == ShaderType::Lighting) {
        Albedo = float3(0.0f, 1.0f, 0.0f);
    } else if (material.ShaderType == ShaderType::Effect) {
        Albedo = float3(0.0f, 0.0f, 1.0f);
    } else {
        Albedo = float3(1.0f, 1.0f, 1.0f);
    }
#elif defined(DEBUG_NOSAMPLING)
    Albedo = float3(0.5f, 0.5f, 0.5f);
#else
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.BaseTexture())];

    vertexColor = saturate(vertexColor / max(max(vertexColor.r, vertexColor.g), vertexColor.b));
    
    const bool isWindows = (material.Feature == Feature::kGlowMap || material.PBRFlags & PBR::Flags::HasEmissive) && material.ShaderFlags & ShaderFlags::kAssumeShadowmask;
    float3 windowAlpha = float3(0.0f, 0.0f, 0.0f);

    float alpha = 1.0f;
    
    [branch]
    if (material.ShaderType == ShaderType::TruePBR)
    {
        Texture2D rmaosTexture = Textures[NonUniformResourceIndex(material.RMAOSTexture())];
        Texture2D emissiveTexture = Textures[NonUniformResourceIndex(material.EmissiveTexture())];

        float4 albedo = baseTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel) * material.BaseColor();
        alpha = albedo.a;
        
        float4 rmaos = rmaosTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
        float3 emissive = emissiveTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;

        if (isWindows)
        {
            windowAlpha = emissive;
        }

        surface.Albedo = albedo.rgb * vertexColor.rgb;
        surface.Emissive = emissive * EmitColorToLinear(material.EffectColor().rgb) * material.EffectColor().a * EmitColorMult() * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        surface.Roughness = saturate(rmaos.x * material.RoughnessScale());
        surface.Metallic = saturate(rmaos.y);
        surface.AO = rmaos.z;
        surface.F0 = material.SpecularLevel() * rmaos.w;

        if ((material.PBRFlags & PBR::Flags::Subsurface) && !(material.ShaderFlags & ShaderFlags::kTwoSided))
        {
            Texture2D subsurfaceTexture = Textures[NonUniformResourceIndex(material.SubsurfaceTexture())];

            float4 subsurfaceColor = subsurfaceTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
            float thickness = subsurfaceColor.a * material.SubsurfaceScale();
            surface.SubsurfaceData.ScatteringColor = subsurfaceColor.rgb * material.SubsurfaceScatteringColor().rgb;
            surface.SubsurfaceData.TransmissionColor = surface.Albedo;

            surface.TransmissionColor = surface.SubsurfaceData.ScatteringColor;

            surface.SubsurfaceData.Scale = 40.0f;
            surface.SubsurfaceData.Anisotropy = 0.0f;

            surface.SubsurfaceData.HasSubsurface = any(surface.SubsurfaceData.ScatteringColor) > 0.0f ? 1 : 0;
        }
        else if ((material.PBRFlags & PBR::Flags::Subsurface) && (material.ShaderFlags & ShaderFlags::kTwoSided))
        {
            // Two sided subsurface - for leaves and thin objects
            Texture2D subsurfaceTexture = Textures[NonUniformResourceIndex(material.SubsurfaceTexture())];
            // Just use simple diffuse transmission for thin objects
            float4 subsurfaceColor = subsurfaceTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
            float thickness = subsurfaceColor.a * material.SubsurfaceScale();
            surface.TransmissionColor = subsurfaceColor.rgb * material.SubsurfaceScatteringColor().rgb;
            surface.DiffTrans = 1 - thickness;
        }

        // Coat (TwoLayer)
        if (material.PBRFlags & PBR::Flags::TwoLayer)
        {
            half4 coatColorParam = material.CoatColor();
            surface.CoatColor = coatColorParam.rgb;
            surface.CoatStrength = coatColorParam.a;
            surface.CoatRoughness = material.CoatRoughness();
            surface.CoatF0 = float3(0.04, 0.04, 0.04);

            if (material.PBRFlags & PBR::Flags::HasFeatureTexture0)
            {
                Texture2D coatColorTexture = Textures[NonUniformResourceIndex(material.CoatColorTexture())];
                float4 sampledCoat = coatColorTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                surface.CoatColor *= sampledCoat.rgb;
                surface.CoatStrength *= sampledCoat.a;
            }

            if (material.PBRFlags & PBR::Flags::HasFeatureTexture1)
            {
                Texture2D coatNormalTexture = Textures[NonUniformResourceIndex(material.CoatNormalTexture())];
                float4 sampledCoatNormal = coatNormalTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
                surface.CoatRoughness *= sampledCoatNormal.a;

                if (material.PBRFlags & PBR::Flags::CoatNormal)
                {
                    NormalMap(
                        sampledCoatNormal.xyz,
                        handedness,
                        normalWS, tangentWS, bitangentWS,
                        surface.CoatNormal, surface.CoatTangent, surface.CoatBitangent
                    );
                }
            }
        }
    }
    else if (material.ShaderType == ShaderType::Lighting)
    {
        float4 diffuse = baseTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel);
        alpha = diffuse.a * material.BaseColor().a;
        
        surface.Albedo = VanillaDiffuseColor(diffuse.rgb * vertexColor.rgb);

        if (material.Feature == Feature::kHairTint)
        {
            float3 hairTint = material.BaseColor().rgb;
            surface.Albedo *= VanillaDiffuseColor(hairTint);
        }
    
        [branch]
        if (material.ShaderFlags & ShaderFlags::kSpecular)
        {
            float3 specularColor = material.SpecularColor().rgb;
            
            [branch]
            if (material.ShaderFlags & ShaderFlags::kModelSpaceNormals)
            {
                Texture2D specularTexture = Textures[NonUniformResourceIndex(material.SpecularTexture())];
                specularColor *= specularTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).r;
            }
            else
            {
                Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
                specularColor *= normalTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).a;
            }
    
#if ALT_PBR_CONV_METALLIC || ALT_PBR_CONV_ROUGHNESS
	        float specularity = CalcSpecularity(specularColor, material.SpecularColor().a);            
	        float roughnessFromShininess = material.RoughnessScale();            
#endif
            
#if ALT_PBR_CONV_METALLIC          
            surface.Metallic = CalcMetallic(surface.Albedo, specularity, roughnessFromShininess);
#endif
            
#if ALT_PBR_CONV_ROUGHNESS
            surface.Roughness =  CalcRoughness(roughnessFromShininess, specularity);
            surface.F0 = clamp(0.08f * specularColor, 0.02f, 0.08f);           
#else
            surface.Roughness = material.RoughnessScale();
            surface.F0 = clamp(0.08f * specularColor * material.SpecularColor().a, 0.02f, 0.08f);
#endif       
        }
         
        [branch]
        if (material.ShaderFlags & ShaderFlags::kEnvMap || material.ShaderFlags & ShaderFlags::kEyeReflect)
        {
            Texture2D envMaskTexture = Textures[NonUniformResourceIndex(material.EnvMaskTexture())];
            float envMask = envMaskTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).r;

            surface.Roughness = max(lerp(surface.Roughness, 0.0f, envMask), 0.08f);
        }

        [branch]
        if (material.Feature == Feature::kGlowMap)
        {
            Texture2D glowTexture = Textures[NonUniformResourceIndex(material.GlowTexture())];
            float3 glow = glowTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;
                
            if (isWindows)
            {
                windowAlpha = glow;
            }
            surface.Emissive = GlowToLinear(glow) * EmitColorToLinear(material.EffectColor().rgb) * material.EffectColor().a * EmitColorMult() * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        }
        else
        {
            surface.Emissive = surface.Albedo * EmitColorToLinear(material.EffectColor().rgb) * material.EffectColor().a * EmitColorMult() * (surface.Primary ? 1.0f : LIGHTINGSETTINGS.Emissive);
        }

        [branch]
        if (material.Feature == Feature::kFaceGen)
        {
            Texture2D detailTexture = Textures[NonUniformResourceIndex(material.DetailTexture())];
            float3 detailColor = detailTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;
            detailColor = float3(3.984375, 3.984375, 3.984375) * (float3(0.00392156886, 0, 0.00392156886) + detailColor);

            Texture2D tintTexture = Textures[NonUniformResourceIndex(material.TintTexture())];
            float3 tintColor = tintTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;
            tintColor = tintColor * surface.Albedo * 2.0f;
            tintColor = tintColor - tintColor * surface.Albedo;
            surface.Albedo = (surface.Albedo * surface.Albedo + tintColor) * detailColor;
                
        }
        else if (material.Feature == Feature::kFaceGenRGBTint)
        {
            float3 tintColor = material.BaseColor().rgb * surface.Albedo * 2.0f;
            tintColor = tintColor - tintColor * surface.Albedo;
            surface.Albedo = float3(1.01171875f, 0.99609375f, 1.01171875f) * (surface.Albedo * surface.Albedo + tintColor);
        }

        [branch]
        if (material.Feature == Feature::kFaceGen || material.Feature == Feature::kFaceGenRGBTint)
        {
            surface.F0 = 0.02776f;
            surface.Metallic = 0.0f;
            surface.SubsurfaceData.HasSubsurface = 1;
            surface.SubsurfaceData.Anisotropy = -0.5f;

        // Typical skin values
            surface.SubsurfaceData.ScatteringColor = float3(4.820f, 1.690f, 1.090f);
            surface.SubsurfaceData.TransmissionColor = surface.Albedo;
            surface.SubsurfaceData.Scale = 1.f;
        }

        [branch]
        if (material.Feature == Feature::kEye)
        {
            surface.Roughness = 0.08f;
            surface.F0 = 0.02776f;
            surface.Metallic = 0.0f;
            surface.SubsurfaceData.HasSubsurface = 1;
            surface.SubsurfaceData.Anisotropy = -0.5f;
        // Typical eye values
            surface.SubsurfaceData.ScatteringColor = float3(1.0f, 0.8f, 0.6f);
            surface.SubsurfaceData.TransmissionColor = surface.Albedo;
            surface.SubsurfaceData.Scale = 1.f;
        }

        [branch]
        if (material.ShaderFlags & ShaderFlags::kRefraction) // As glass
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
    else if (material.ShaderType == ShaderType::Effect)
    {
        float3 base = float3(1, 1, 1);

        [branch]
        if (material.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
        {
            base *= baseTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).rgb;
        }

        float3 baseColorMul = material.EffectColor().rgb;

        [branch]
        if (material.ShaderFlags & ShaderFlags::kVertexColors && !(material.ShaderFlags & ShaderFlags::kProjectedUV))
        {
            base *= vertexColor.rgb;
        }

        float3 baseColor = base * baseColorMul;

        float baseColorScale = material.EffectColor().a;

        [branch]
        if (material.ShaderFlags & ShaderFlags::kGrayscaleToPaletteColor)
        {
            Texture2D effectTexture = Textures[NonUniformResourceIndex(material.EffectTexture())];

            float2 grayscaleToColorUv = float2(base.g, baseColorMul.x);

            baseColor = baseColorScale * effectTexture.SampleLevel(DefaultSampler, grayscaleToColorUv, mipLevel).rgb;
        }

        float3 baseColorLinear = EffectToLinear(baseColor);

    //Albedo = baseColorLinear; // This breaks sharc
        surface.Albedo = 0;
        surface.Emissive = baseColorLinear * LIGHTINGSETTINGS.Effect;
    }
    else
    {
        surface.Albedo = float3(1.0f, 0.0f, 1.0f);
    }

    [branch]
    if (material.AlphaFlags != AlphaFlags::None)
    {
        [branch]
        if ((material.ShaderFlags & ShaderFlags::kVertexAlpha) && !(material.ShaderFlags & ShaderFlags::kTreeAnim))
            alpha *= vertexColor.a;

        [branch]
        if (material.AlphaFlags & AlphaFlags::Transmission)
        {
            surface.TransmissionColor = lerp(float3(1.0f, 1.0f, 1.0f), surface.Albedo, alpha);
            surface.Albedo *= alpha;
            surface.Metallic *= alpha;
            surface.SpecTrans = 1.0f;
            surface.IsThinSurface |= (material.ShaderFlags & ShaderFlags::kTwoSided) != 0;
            if (material.ShaderType != ShaderType::TruePBR)
            {
                surface.Roughness = 0.0f;
            }
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

#endif

#if defined(DEBUG_NONORMALMAP)
    Normal = normalWS;
    Tangent = tangentWS;
    Bitangent = bitangentWS;
#else
    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
    float3 normal = normalTexture.SampleLevel(DefaultSampler, texCoord0, mipLevel).xyz;

    NormalMap(
        normal,
        handedness,
        normalWS, tangentWS, bitangentWS,
        surface.Normal, surface.Tangent, surface.Bitangent
    );
#endif

    // Hair flowmap processing
#if HAIR_MODE
    [branch]
    if (material.Feature == Feature::kHairTint && HAIRSETTINGS.Enabled) {
        surface.Roughness = 1.0f - saturate(HAIRSETTINGS.HairGlossiness * 0.01f);
        surface.Albedo = saturate(surface.Albedo * HAIRSETTINGS.BaseColorMult);
        [branch]
        if (material.ShaderFlags & ShaderFlags::kBackLighting) {
            Texture2D hairFlowMapTexture = Textures[NonUniformResourceIndex(material.SpecularTexture())];
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
}

void WaterMaterial(inout Surface surface, in float2 texCoord0, in float3 tangentWS, in float3 bitangentWS, in float handedness, in Material material)
{
    const float mipLevel = surface.MipLevel;

    surface.Albedo = float3(1.0f, 1.0f, 1.0f);
    surface.Roughness = 0.0f;
    surface.Metallic = 0.0f;
    surface.F0 = 0.02f;
    surface.IOR = 1.33f;
 
    Texture2D normals01Texture = Textures[NonUniformResourceIndex(material.Texture0)];
    Texture2D normals02Texture = Textures[NonUniformResourceIndex(material.Texture1)];
    Texture2D normals03Texture = Textures[NonUniformResourceIndex(material.Texture2)];

    float2 normalScroll0 = material.Vector0.xy;
    float2 normalScroll1 = material.Vector0.zw;
    float2 normalScroll2 = material.Vector1.xy;
    
    const float scale = 0.001f;
    
    float normalsScale0 = scale * material.Vector1.z;
    float normalsScale1 = scale * material.Vector1.w;
    float normalsScale2 = scale * material.Vector2.x;
    
    float2 normal0TexCoord = (texCoord0 / normalsScale0) + normalScroll0;
    float2 normal1TexCoord = (texCoord0 / normalsScale1) + normalScroll1;
    float2 normal2TexCoord = (texCoord0 / normalsScale2) + normalScroll2;
    
    float3 normals1 = normals01Texture.SampleLevel(DefaultSampler, normal0TexCoord, mipLevel).xyz * 2.0 + float3(-1, -1, -2);
    float3 normals2 = normals02Texture.SampleLevel(DefaultSampler, normal1TexCoord, mipLevel).xyz * 2.0 - 1.0;
    float3 normals3 = normals03Texture.SampleLevel(DefaultSampler, normal2TexCoord, mipLevel).xyz * 2.0 - 1.0;

    surface.Normal = normalize(
        float3(0, 0, 1) +
        material.Scalar0 * normals1 +
        material.Scalar1 * normals2 +
        material.Scalar2 * normals3
    );
    
    surface.Tangent = normalize(tangentWS - surface.Normal * dot(tangentWS, surface.Normal));
    surface.Bitangent = cross(surface.Normal, surface.Tangent) * handedness;
    
    // Distance-based absorption via Beer-Lambert law instead of flat surface tint.
    // The absorption coefficient is derived from the game's water color at a reference depth.
    static const float WATER_ABSORPTION_REFERENCE_DEPTH = 600.0;
    float3 waterColor = saturate(material.Color0.rgb);
    surface.VolumeAbsorption = -log(max(waterColor, 1e-4)) / WATER_ABSORPTION_REFERENCE_DEPTH;
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

    void LandMaterial(inout Surface surface, in float2 texCoord0, in float4 vertexColor, float3 normalWS, float3 tangentWS, float3 bitangentWS, in float handedness, float4 landBlend0, float4 landBlend1, in Material material)
{
    float mipLevel = surface.MipLevel;
    
    Texture2D overlayTexture = Textures[NonUniformResourceIndex(material.OverlayTexture())];
    Texture2D noiseTexture = Textures[NonUniformResourceIndex(material.NoiseTexture())];

	// Normalise blend weights
    float totalWeight = landBlend0.x + landBlend0.y + landBlend0.z +
	                    landBlend0.w + landBlend1.x + landBlend1.y;

    landBlend0 /= totalWeight;
    landBlend1.xy /= totalWeight;

    float3 baseColor = BlendLandTexture(material.Texture0, texCoord0, landBlend0.x, mipLevel).rgb + BlendLandTexture(material.Texture1, texCoord0, landBlend0.y, mipLevel).rgb +
                        BlendLandTexture(material.Texture2, texCoord0, landBlend0.z, mipLevel).rgb + BlendLandTexture(material.Texture3, texCoord0, landBlend0.w, mipLevel).rgb +
                        BlendLandTexture(material.Texture4, texCoord0, landBlend1.x, mipLevel).rgb + BlendLandTexture(material.Texture5, texCoord0, landBlend1.y, mipLevel).rgb;

    baseColor *= vertexColor.rgb;

    [branch]
    if (material.ShaderType == ShaderType::TruePBR)
    {
        surface.Albedo = baseColor;

        float4 rmaos = BlendLandTexture(material.Texture12, texCoord0, landBlend0.x, mipLevel) + BlendLandTexture(material.Texture13, texCoord0, landBlend0.y, mipLevel) +
                        BlendLandTexture(material.Texture14, texCoord0, landBlend0.z, mipLevel) + BlendLandTexture(material.Texture15, texCoord0, landBlend0.w, mipLevel) +
                        BlendLandTexture(material.Texture16, texCoord0, landBlend1.x, mipLevel) + BlendLandTexture(material.Texture17, texCoord0, landBlend1.y, mipLevel);

        surface.Roughness = saturate(rmaos.x * 1.0f); // material.RoughnessScale()
        surface.Metallic = saturate(rmaos.y);
        surface.AO = rmaos.z;
        surface.F0 = PBR::Defaults::F0 * rmaos.w; //material.SpecularLevel()
    }
    else if (material.ShaderType == ShaderType::Lighting)
    {
        surface.Albedo = baseColor; // GammaToTrueLinear looks wonky
    }

#if defined(DEBUG_NONORMALMAP)
    Normal = normalWS;
    Tangent = tangentWS;
    Bitangent = bitangentWS;
#else          
    float3 normal = BlendLandTexture(material.Texture6, texCoord0, landBlend0.x, mipLevel).rgb + BlendLandTexture(material.Texture7, texCoord0, landBlend0.y, mipLevel).rgb +
                    BlendLandTexture(material.Texture8, texCoord0, landBlend0.z, mipLevel).rgb + BlendLandTexture(material.Texture9, texCoord0, landBlend0.w, mipLevel).rgb +
                    BlendLandTexture(material.Texture10, texCoord0, landBlend1.x, mipLevel).rgb + BlendLandTexture(material.Texture11, texCoord0, landBlend1.y, mipLevel).rgb;
        
    NormalMap(
        normal,
        handedness,
        normalWS, tangentWS, bitangentWS,
        surface.Normal, surface.Tangent, surface.Bitangent
    );
#endif        
}

#endif // SURFACE_SKYRIM_HLSL