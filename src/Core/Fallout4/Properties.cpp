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
	auto* shaderProperty = runtimeData.shaderProperty;
	auto shaderRuntime = Util::Adapter::GetShaderPropertyRuntimeData(shaderProperty);
	AlphaFlags alphaFlags = AlphaFlags::None;
	Feature feature = Feature::kDefault;
	bool isWater = false;

	if (runtimeData.alphaProperty) {
		const auto alphaPropertyFlags = Util::Adapter::GetAlphaPropertyFlags(runtimeData.alphaProperty);
		if (alphaPropertyFlags & 1)
			alphaFlags |= AlphaFlags::Blend;
		if ((alphaPropertyFlags & 0x100) || (shaderRuntime.flags & static_cast<uint64_t>(EShaderPropertyFlag::kAlphaTest))) {
			alphaFlags |= AlphaFlags::Test;
			m_Data.AlphaThreshold = Util::Adapter::GetAlphaTestReference(runtimeData.alphaProperty) / 255.0f;
		}
	}

	if (!shaderProperty)
		return;

	m_Data.ShaderFlags = MapShaderFlags(shaderProperty);
	m_Data.Alpha = shaderRuntime.alpha;

	const auto materialType = shaderRuntime.materialType;
	if (materialType == 3) {
		isWater = true;
		m_Data.WaterFlags = MapWaterShaderFlags(reinterpret_cast<RE::BSWaterShaderProperty*>(shaderProperty));
	}
	else if (materialType == 2) {
		if (shaderRuntime.material)
			feature = shaderRuntime.material->GetFeature();

		if (feature == Feature::kSnow)
			m_Data.ShaderFlags |= ShaderFlags::kSnow;

		m_Data.EmissiveColor.x = shaderRuntime.emissiveColor.x;
		m_Data.EmissiveColor.y = shaderRuntime.emissiveColor.y;
		m_Data.EmissiveColor.z = shaderRuntime.emissiveColor.z;
		m_Data.EmissiveColor.w = shaderRuntime.emissiveScale;

			if (shaderRuntime.flags & static_cast<uint64_t>(EShaderPropertyFlag::kProjectedUV)) {
				auto params = shaderRuntime.projectedUVParams;
				const float oneMinusAlpha = 1.0f - params.w;
				m_Data.ProjectedUVParams = half4(oneMinusAlpha * params.x, 0.0f, params.z, (oneMinusAlpha * params.y) + params.w);
				m_Data.ProjectedUVParams2 = half4(shaderRuntime.projectedUVColor);
				m_Data.TextureProj = half4(0.0f, 0.0f, 1.0f, 0.0f);
			}
	}

	const bool isEyeFeature = feature == Feature::kEye || (feature == Feature::kEnvmap && isEye);
	const bool blendMaterial = feature == Feature::kHairTint || feature == Feature::kFace ||
		feature == Feature::kSkinTint || isEyeFeature ||
		(shaderRuntime.flags & (static_cast<uint64_t>(EShaderPropertyFlag::kDecal) | static_cast<uint64_t>(EShaderPropertyFlag::kDynamicDecal)));

	if (shaderRuntime.flags & static_cast<uint64_t>(EShaderPropertyFlag::kPremultAlpha)) {
		alphaFlags &= ~AlphaFlags::Blend;
		alphaFlags |= AlphaFlags::Transmission;
	}
	else if ((alphaFlags & AlphaFlags::Blend) != AlphaFlags::None && !blendMaterial) {
		alphaFlags &= ~AlphaFlags::Blend;
		alphaFlags |= AlphaFlags::Transmission;
	}

	if (alphaFlags == AlphaFlags::None && (shaderRuntime.flags & static_cast<uint64_t>(EShaderPropertyFlag::kRefraction) || isWater))
		alphaFlags |= AlphaFlags::Transmission;

	m_Data.AlphaFlags = alphaFlags;
}

uint32_t Properties::MapShaderFlags(RE::BSShaderProperty* shaderProperty)
{
	const auto flags = Util::Adapter::GetShaderPropertyRuntimeData(shaderProperty).flags;
	uint32_t result = 0;

	const auto has = [flags](EShaderPropertyFlag a_flag) { return flags & static_cast<uint64_t>(a_flag); };
	if (has(EShaderPropertyFlag::kSpecular)) result |= kSpecular;
	if (has(EShaderPropertyFlag::kVertexAlpha)) result |= kVertexAlpha;
	if (has(EShaderPropertyFlag::kGrayscaleToPaletteColor)) result |= kGrayscaleToPaletteColor;
	if (has(EShaderPropertyFlag::kGrayscaleToPaletteAlpha)) result |= kGrayscaleToPaletteAlpha;
	if (has(EShaderPropertyFlag::kFalloff)) result |= kFalloff;
	if (has(EShaderPropertyFlag::kEnvMap)) result |= kEnvMap;
	if (has(EShaderPropertyFlag::kFace)) result |= kFace;
	if (has(EShaderPropertyFlag::kModelSpaceNormals)) result |= kModelSpaceNormals;
	if (has(EShaderPropertyFlag::kRefraction)) result |= kRefraction;
	if (has(EShaderPropertyFlag::kProjectedUV)) result |= kProjectedUV;
	if (has(EShaderPropertyFlag::kExternalEmittance)) result |= kExternalEmittance;
	if (has(EShaderPropertyFlag::kVertexColors)) result |= kVertexColors;
	if (has(EShaderPropertyFlag::kMultiTextureLandscape)) result |= kMultiTextureLandscape;
	if (has(EShaderPropertyFlag::kEyeReflect)) result |= kEyeReflect;
	if (has(EShaderPropertyFlag::kHairTint)) result |= kHairTint;
	if (has(EShaderPropertyFlag::kTwoSided)) result |= kTwoSided;
	if (has(EShaderPropertyFlag::kTreeAnim)) result |= kTreeAnim;
	if (has(EShaderPropertyFlag::kLODLandscape)) result |= kLODLandscape;
	if (has(EShaderPropertyFlag::kLODObjects)) result |= kLODObjects;

	return result;
}

uint16_t Properties::MapWaterShaderFlags(RE::BSWaterShaderProperty* waterShaderProp)
{
	(void)waterShaderProp;
	return 0;
}

#endif
