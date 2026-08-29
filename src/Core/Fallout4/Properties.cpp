#if defined(FALLOUT4)

#include "Core/Skyrim/Properties.h"

#include "Scene.h"
#include "Utils/Adapter.h"

Properties::Properties()
{
	m_Data.ShaderFlags = 0;
	m_Data.AlphaFlags = AlphaFlags::None;
	m_Data.AlphaThreshold = 0.5f;
	m_Data.Alpha = 1.0f;
	m_Data.EmissiveColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams = half4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams2 = half4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams3 = half4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.TextureProj = half4(0.0f, 0.0f, 1.0f, 0.0f);
}

void Properties::Update(RE::BSTriShape* triShape, bool isEye)
{
	auto runtimeData = Util::Adapter::GetGeometryRuntimeData(triShape);

	AlphaFlags alphaFlags = AlphaFlags::None;
	Feature feature = Feature::kDefault;
	bool isWater = false;

	auto alphaProperty = runtimeData.alphaProperty;
	if (alphaProperty) {
		const auto alphaPropertyFlags = alphaProperty->flags.flags;
		if (alphaPropertyFlags & static_cast<uint16_t>(RE::NiAlphaPropertyFlags::kAlphaBlend))
			alphaFlags |= AlphaFlags::Blend;

		if (alphaPropertyFlags & static_cast<uint16_t>(RE::NiAlphaPropertyFlags::kAlphaTest)) {
			alphaFlags |= AlphaFlags::Test;
			m_Data.AlphaThreshold = alphaProperty->alphaTestRef / 255.0f;
		}
	}

	auto* shaderProperty = runtimeData.shaderProperty;
	if (shaderProperty) {
		const auto& shaderFlags = shaderProperty->flags;

		m_Data.ShaderFlags = MapShaderFlags(shaderProperty);
		m_Data.Alpha = shaderProperty->alpha;

		const auto materialType = shaderProperty->GetMaterialType();
		if (materialType == 3) {
			isWater = true;
			m_Data.WaterFlags = MapWaterShaderFlags(reinterpret_cast<RE::BSWaterShaderProperty*>(shaderProperty));
		}
		else if (materialType == 2) {
			auto* lightingProperty = reinterpret_cast<RE::BSLightingShaderProperty*>(shaderProperty);

			if (shaderProperty->material)
				feature = shaderProperty->material->GetFeature();

			if (feature == Feature::kSnow)
				m_Data.ShaderFlags |= ShaderFlags::kSnow;

			if (lightingProperty->emitColor) {
				m_Data.EmissiveColor.x = lightingProperty->emitColor->r;
				m_Data.EmissiveColor.y = lightingProperty->emitColor->g;
				m_Data.EmissiveColor.z = lightingProperty->emitColor->b;
			}

			m_Data.EmissiveColor.w = lightingProperty->emitColorScale;

			if (lightingProperty->flags.all(EShaderPropertyFlag::kProjectedUV)) {
				auto params = Util::Math::Float4(lightingProperty->projectedUVParams);
				float oneMinusAlpha = 1.0f - params.w;

				m_Data.ProjectedUVParams = half4(oneMinusAlpha * params.x, 0.0f, params.z, (oneMinusAlpha * params.y) + params.w);
				m_Data.ProjectedUVParams2 = Util::Math::Float4(lightingProperty->projectedUVColor);

				const auto& iniSettings = Scene::GetSingleton()->m_INISettings;

				// All yoinked from Nukem 
				// https://github.com/Nukem9/skyrimse-test/blob/328916305165a46c4e4b527735bbcfd46b09a0ca/skyrim64_test/src/patches/TES/BSShader/Shaders/BSLightingShader.cpp#L883
				{
					auto renderFlags = 0;
					bool enableProjectedNormals = iniSettings.enableProjecteUVDiffuseNormals && (!(renderFlags & 0x8) || !iniSettings.enableProjecteUVDiffuseNormalsOnCubemap);

					m_Data.ProjectedUVParams3 = half4(
						iniSettings.projectedUVDiffuseNormalTilingScale,
						iniSettings.projectedUVNormalDetailTilingScale,
						0.0f,
						enableProjectedNormals ? 1.0f : 0.0f
					);
				}

				// Texture Projection - Non-Default if BSGeometry::IsMultiIndexTriShape() is true
				m_Data.TextureProj = half4(0.0f, 0.0f, 1.0f, 0.0f);
			}
		}

		const bool isEyeFeature = feature == Feature::kEye || (feature == Feature::kEnvmap && isEye);
		const bool blendMaterial = feature == Feature::kHairTint || feature == Feature::kFace ||
			feature == Feature::kSkinTint || isEyeFeature ||
			shaderFlags.any(EShaderPropertyFlag::kDecal, EShaderPropertyFlag::kDynamicDecal);

		if ((alphaFlags & AlphaFlags::Additive) != AlphaFlags::None) {
			alphaFlags &= ~AlphaFlags::Blend;
			alphaFlags |= AlphaFlags::Transmission;
		}
		else if ((alphaFlags & AlphaFlags::Blend) != AlphaFlags::None && !blendMaterial) {
			alphaFlags &= ~AlphaFlags::Blend;
			alphaFlags |= AlphaFlags::Transmission;
		}

		if (alphaFlags == AlphaFlags::None) {
			if (shaderFlags.any(EShaderPropertyFlag::kRefraction) || isWater)
				alphaFlags |= AlphaFlags::Transmission;
		}
	}

	m_Data.AlphaFlags = alphaFlags;
}

uint32_t Properties::MapShaderFlags(RE::BSShaderProperty* shaderProperty)
{
	const auto flags = shaderProperty->flags;
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
	if (flags.any(EShaderPropertyFlag::kTreeAnim)) result |= kTreeAnim;
	if (flags.any(EShaderPropertyFlag::kLODLandscape)) result |= kLODLandscape;
	if (flags.any(EShaderPropertyFlag::kLODObjects)) result |= kLODObjects;
	if (flags.any(EShaderPropertyFlag::kOwnEmit)) result |= kOwnEmit;
	return result;
}

uint16_t Properties::MapWaterShaderFlags(RE::BSWaterShaderProperty* waterShaderProp)
{
	(void)waterShaderProp;
	return 0;
}

#endif
