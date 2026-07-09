#include "Core/Skyrim/Properties.h"

#include "Util.h"

#include "Scene.h"

#include "Types/WaterFlags.h"

Properties::Properties(RE::BSTriShape* triShape)
{
	m_Data.ShaderFlags = 0;
	m_Data.AlphaFlags = AlphaFlags::None;
	m_Data.AlphaThreshold = 0.5f;
	m_Data.Alpha = 1.0f;
	m_Data.EmissiveColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams0 = half4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams1 = half4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams2 = half4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams3 = half4(0.0f, 0.0f, 0.0f, 0.0f);

	if (!triShape)
		return;

	auto runtimeData = Util::Adapter::GetGeometryRuntimeData(triShape);

	AlphaFlags alphaFlags = AlphaFlags::None;
	Feature feature = Feature::kDefault;
	bool hasPbrEmissive = false;
	bool isWater = false;

	auto alphaProperty = runtimeData.alphaProperty;
	if (alphaProperty) {
		if (alphaProperty->GetAlphaBlending()) {
			using AlphaFunction = RE::NiAlphaProperty::AlphaFunction;

			if (alphaProperty->GetDestBlendMode() == AlphaFunction::kOne)
				alphaFlags |= AlphaFlags::Additive | AlphaFlags::Transmission;
			else
				alphaFlags |= AlphaFlags::Blend;
		}

		if (alphaProperty->GetAlphaTesting()) {
			alphaFlags |= AlphaFlags::Test;
			m_Data.AlphaThreshold = alphaProperty->alphaThreshold / 255.0f;
		}
	}

	auto shaderProperty = runtimeData.shaderProperty;
	if (shaderProperty) {
		m_Data.ShaderFlags = MapShaderFlags(shaderProperty);
		m_Data.Alpha = shaderProperty->alpha;

		const auto materialType = shaderProperty->GetMaterialType();
		if (materialType == RE::BSShaderMaterial::Type::kWater) {
			auto waterShaderProperty = reinterpret_cast<RE::BSWaterShaderProperty*>(shaderProperty);
			m_Data.WaterFlags = MapWaterShaderFlags(waterShaderProperty);

			auto scene = Scene::GetSingleton();

			int32_t flowMapSize = *scene->g_FlowMapSize;

			// CellTexCoordOffset - Flowmap
			m_Data.ProjectedUVParams0 = {
				static_cast<float>(waterShaderProperty->flowX),
				static_cast<float>(flowMapSize - waterShaderProperty->flowY - 1),
				static_cast<float>(waterShaderProperty->cellX),
				static_cast<float>(-waterShaderProperty->cellY)
			};

			isWater = true;
		}
		else {
			if (materialType == RE::BSShaderMaterial::Type::kLighting) {
				auto lightingShaderProp = reinterpret_cast<RE::BSLightingShaderProperty*>(shaderProperty);

				if (auto shaderMaterial = lightingShaderProp->material) {
					feature = shaderMaterial->GetFeature();

					if (typeid(*shaderMaterial) == typeid(BSLightingShaderMaterialPBR)) {
						const auto pbrFlags = Util::Material::Skyrim::GetPBRShaderFlags(static_cast<BSLightingShaderMaterialPBR*>(shaderMaterial));
						hasPbrEmissive = pbrFlags.any(PBRShaderFlags::HasEmissive);
					}
				}
				if (lightingShaderProp->emissiveColor) {
					m_Data.EmissiveColor.x = lightingShaderProp->emissiveColor->red;
					m_Data.EmissiveColor.y = lightingShaderProp->emissiveColor->green;
					m_Data.EmissiveColor.z = lightingShaderProp->emissiveColor->blue;
				}

				m_Data.EmissiveColor.w = lightingShaderProp->emissiveMult;

				if (lightingShaderProp->flags.all(EShaderPropertyFlag::kProjectedUV)) {
					auto params = Util::Math::Float4(lightingShaderProp->projectedUVParams);
					float oneMinusAlpha = 1.0f - params.w;

					m_Data.ProjectedUVParams0 = half4(oneMinusAlpha * params.x, 0.0f, params.z, (oneMinusAlpha * params.y) + params.w);
					m_Data.ProjectedUVParams1 = Util::Math::Float4(lightingShaderProp->projectedUVColor);

					const auto& iniSettings = Scene::GetSingleton()->m_INISettings;

					// All yoinked from Nukem 
					// https://github.com/Nukem9/skyrimse-test/blob/328916305165a46c4e4b527735bbcfd46b09a0ca/skyrim64_test/src/patches/TES/BSShader/Shaders/BSLightingShader.cpp#L883
					{
						auto renderFlags = 0;
						bool enableProjectedNormals = iniSettings.enableProjecteUVDiffuseNormals && (!(renderFlags & 0x8) || !iniSettings.enableProjecteUVDiffuseNormalsOnCubemap);

						m_Data.ProjectedUVParams2 = half4(
							iniSettings.projectedUVDiffuseNormalTilingScale,
							iniSettings.projectedUVNormalDetailTilingScale,
							0.0f,
							enableProjectedNormals ? 1.0f : 0.0f
						);
					}

					// Texture Projection - Non-Default if BSGeometry::IsMultiIndexTriShape() is true
					m_Data.ProjectedUVParams3 = half4(0.0f, 0.0f, 1.0f, 0.0f);
				}
			}
		}
	}

	if (shaderProperty) {
		const auto& shaderFlags = shaderProperty->flags;

		bool blendMaterial = feature == Feature::kHairTint || feature == Feature::kFaceGen || feature == Feature::kFaceGenRGBTint || feature == Feature::kEye;
		blendMaterial |= shaderFlags.any(EShaderPropertyFlag::kDecal, EShaderPropertyFlag::kDynamicDecal);

		if ((alphaFlags & AlphaFlags::Additive) != AlphaFlags::None) {
			alphaFlags &= ~AlphaFlags::Blend;
			alphaFlags |= AlphaFlags::Transmission;
		}
		else if ((alphaFlags & AlphaFlags::Blend) != AlphaFlags::None && !blendMaterial) {
			alphaFlags &= ~AlphaFlags::Blend;
			alphaFlags |= AlphaFlags::Transmission;
		}

		if (alphaFlags == AlphaFlags::None) {
			const bool isRefraction = shaderFlags.any(EShaderPropertyFlag::kRefraction);
			const bool isWindow = (feature == Feature::kGlowMap || hasPbrEmissive) && shaderFlags.any(EShaderPropertyFlag::kAssumeShadowmask);

			if (isRefraction || isWindow || isWater)
				alphaFlags |= AlphaFlags::Transmission;
		}
	}

	m_Data.AlphaFlags = alphaFlags;
}

uint32_t Properties::MapShaderFlags(RE::BSShaderProperty* shaderProperty)
{
	const auto& flags = shaderProperty->flags;

	uint32_t result = 0;

	if (flags.any(EShaderPropertyFlag::kSpecular)) result |= kSpecular;
	if (flags.any(EShaderPropertyFlag::kVertexAlpha)) result |= kVertexAlpha;
	if (flags.any(EShaderPropertyFlag::kGrayscaleToPaletteColor)) result |= kGrayscaleToPaletteColor;
	if (flags.any(EShaderPropertyFlag::kGrayscaleToPaletteAlpha)) result |= kGrayscaleToPaletteAlpha;
	if (flags.any(EShaderPropertyFlag::kFalloff)) result |= kFalloff;
	if (flags.any(EShaderPropertyFlag::kEnvMap)) result |= kEnvMap;
	if (flags.any(EShaderPropertyFlag::kFace)) result |= kFace;
	if (flags.any(EShaderPropertyFlag::kModelSpaceNormals)) result |= kModelSpaceNormals;
	if (flags.any(EShaderPropertyFlag::kRefraction)) result |= kRefraction;
	if (flags.any(EShaderPropertyFlag::kProjectedUV)) result |= kProjectedUV;
	if (flags.any(EShaderPropertyFlag::kExternalEmittance)) result |= kExternalEmittance;
	if (flags.any(EShaderPropertyFlag::kVertexColors)) result |= kVertexColors;
	if (flags.any(EShaderPropertyFlag::kMultiTextureLandscape)) result |= kMultiTextureLandscape;
	if (flags.any(EShaderPropertyFlag::kEyeReflect)) result |= kEyeReflect;
	if (flags.any(EShaderPropertyFlag::kHairTint)) result |= kHairTint;
	if (flags.any(EShaderPropertyFlag::kTwoSided)) result |= kTwoSided;
	if (flags.any(EShaderPropertyFlag::kAssumeShadowmask)) result |= kAssumeShadowmask;
	if (flags.any(EShaderPropertyFlag::kBackLighting)) result |= kBackLighting;
	if (flags.any(EShaderPropertyFlag::kTreeAnim)) result |= kTreeAnim;
	if (flags.any(EShaderPropertyFlag::kSoftLighting)) result |= kSoftLighting;
	if (flags.any(EShaderPropertyFlag::kLODLandscape)) result |= kLODLandscape;
	if (flags.any(EShaderPropertyFlag::kLODObjects)) result |= kLODObjects;
	if (flags.any(EShaderPropertyFlag::kHDLODObjects)) result |= kHDLODObjects;
	if (flags.any(EShaderPropertyFlag::kSnow)) result |= kSnow;

	return result;
}

uint16_t Properties::MapWaterShaderFlags(RE::BSWaterShaderProperty* waterShaderProp)
{
	const auto& waterFlags = waterShaderProp->waterFlags.underlying();

	uint16_t result = 0;

	if (waterFlags & WaterFlags::kActorInWater) result |= kActorInWater;
	if (waterFlags & WaterFlags::kActorMovingInWater) result |= kActorMovingInWater;
	if (waterFlags & WaterFlags::kVertexUV) result |= kWaterVertexUV;
	if (waterFlags & WaterFlags::kEnableFlowmap) result |= kWaterEnableFlowmap;
	if (waterFlags & WaterFlags::kBlendNormals) result |= kWaterBlendNormals;
	if (waterFlags & WaterFlags::kDisplacement) result |= kWaterDisplacement;
	if (waterFlags & WaterFlags::kVertexAlphaDepth) result |= kWaterVertexAlphaDepth;
	if (waterFlags & WaterFlags::kDepth) result |= kWaterDepth;

	return result;
}
