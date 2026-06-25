#include "Core/Skyrim/Properties.h"

#include "Util.h"

Properties::Properties(RE::BSTriShape* triShape)
{
	m_Data.ShaderFlags = 0;
	m_Data.AlphaFlags = AlphaFlags::None;
	m_Data.AlphaThreshold = 0.5f;
	m_Data.EmissiveColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.Alpha = 1.0f;
	m_Data.ProjectedUVParams0 = half4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams1 = half4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams2 = half4(0.0f, 0.0f, 0.0f, 0.0f);
	m_Data.ProjectedUVParams3 = half4(0.0f, 0.0f, 0.0f, 0.0f);

	if (!triShape)
		return;

	auto runtimeData = Util::Adapter::GetGeometryRuntimeData(triShape);

	auto alphaProperty = runtimeData.alphaProperty;
	if (alphaProperty) {
		AlphaFlags alphaFlags = AlphaFlags::None;

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

		m_Data.AlphaFlags = alphaFlags;
	}

	auto shaderProperty = runtimeData.shaderProperty;
	if (shaderProperty) {
		m_Data.ShaderFlags = MapShaderFlags(shaderProperty);
		m_Data.Alpha = shaderProperty->alpha;

		if (auto lightingShaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(shaderProperty)) {
			float4 emissive = float4(1.0f, 1.0f, 1.0f, lightingShaderProp->emissiveMult);

			if (lightingShaderProp->flags.all(EShaderPropertyFlag::kOwnEmit) && lightingShaderProp->emissiveColor) {
				emissive.x = lightingShaderProp->emissiveColor->red;
				emissive.y = lightingShaderProp->emissiveColor->green;
				emissive.z = lightingShaderProp->emissiveColor->blue;
			}

			m_Data.EmissiveColor = emissive;

			if (lightingShaderProp->flags.all(EShaderPropertyFlag::kProjectedUV)) {
				auto params = Util::Math::Float4(lightingShaderProp->projectedUVParams);
				float oneMinusAlpha = 1.0f - params.w;

				m_Data.ProjectedUVParams0 = half4(oneMinusAlpha * params.x, 0.0f, params.z, (oneMinusAlpha * params.y) + params.w);
				m_Data.ProjectedUVParams1 = Util::Math::Float4(lightingShaderProp->projectedUVColor);

				auto renderFlags = 0;
				bool enableProjectedNormals = RE::GetINISetting("bEnableProjecteUVDiffuseNormals:Display")->GetBool() && (!(renderFlags & 0x8) || !RE::GetINISetting("bEnableProjecteUVDiffuseNormalsOnCubemap:Display")->GetBool());

				m_Data.ProjectedUVParams2 = half4(
					RE::GetINISetting("fProjectedUVDiffuseNormalTilingScale:Display")->GetFloat(),
					RE::GetINISetting("fProjectedUVNormalDetailTilingScale:Display")->GetFloat(),
					0.0f,
					enableProjectedNormals ? 1.0f : 0.0f
				);

				m_Data.ProjectedUVParams3 = half4(0.0f, 0.0f, 1.0f, 0.0f);
			}
		}
	}
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
