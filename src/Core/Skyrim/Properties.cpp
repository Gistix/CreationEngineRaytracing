#include "Core/Skyrim/Properties.h"

#include "Util.h"

Properties::Properties(RE::BSTriShape* triShape)
{
	m_Data.ShaderFlags = 0;
	m_Data.AlphaFlags = 0;
	m_Data.AlphaThreshold = 0.5f;
	m_Data.EmissiveColor = float4(0.0f, 0.0f, 0.0f, 0.0f);

	if (!triShape)
		return;

	auto runtimeData = Util::Adapter::GetGeometryRuntimeData(triShape);

	*this = Properties(runtimeData.shaderProperty, runtimeData.alphaProperty);
}

Properties::Properties(RE::BSShaderProperty* shaderProperty, RE::NiAlphaProperty* alphaProperty)
{
	if (alphaProperty) {
		AlphaFlags alpha = AlphaFlags::None;

		if (alphaProperty->GetAlphaBlending()) {
			using AlphaFunction = RE::NiAlphaProperty::AlphaFunction;

			if (alphaProperty->GetDestBlendMode() == AlphaFunction::kOne)
				alpha |= AlphaFlags::Additive | AlphaFlags::Transmission;
			else
				alpha |= AlphaFlags::Blend;
		}

		if (alphaProperty->GetAlphaTesting()) {
			alpha |= AlphaFlags::Test;
			m_Data.AlphaThreshold = alphaProperty->alphaThreshold / 255.0f;
		}

		m_Data.AlphaFlags = static_cast<uint16_t>(alpha);
	}

	if (shaderProperty) {
		m_Data.ShaderFlags = MapShaderFlags(shaderProperty);

		if (auto lightingShaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(shaderProperty)) {
			float4 emissive = float4(1.0f, 1.0f, 1.0f, lightingShaderProp->emissiveMult);

			if (lightingShaderProp->flags.all(EShaderPropertyFlag::kOwnEmit) && lightingShaderProp->emissiveColor) {
				emissive.x = lightingShaderProp->emissiveColor->red;
				emissive.y = lightingShaderProp->emissiveColor->green;
				emissive.z = lightingShaderProp->emissiveColor->blue;
			}

			m_Data.EmissiveColor = emissive;
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
