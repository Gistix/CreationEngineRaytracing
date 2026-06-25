#include "Core/Material/Skyrim/PBRMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Utils/Material.h"
#include "Types/CommunityShaders/BSLightingShaderMaterialPBR.h"

PBRMaterial::PBRMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;

	m_Data = eastl::make_unique<PBRMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void PBRMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto pbrData = reinterpret_cast<Data*>(data);

	// GetType() reports kLighting for PBR materials; the actual classification is TruePBR.
	pbrData->Type = MaterialBase::Type::TruePBR;

	auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

	pbrData->PBRFlags = static_cast<uint16_t>(Util::Material::Skyrim::GetPBRShaderFlags(pbrMaterial).underlying());
	pbrData->RoughnessScale = pbrMaterial->GetRoughnessScale();
	pbrData->SpecularLevel = pbrMaterial->GetSpecularLevel();

	// Subsurface / coat / fuzz are mutually exclusive and share FeatureColor / FeatureScalar.
	const auto& pbrFlags = pbrMaterial->pbrFlags;

	if (pbrFlags.any(PBRFlags::Subsurface)) {
		const auto& subsurfaceColor = pbrMaterial->GetSubsurfaceColor();
		pbrData->FeatureColor = float4(subsurfaceColor.red, subsurfaceColor.green, subsurfaceColor.blue, 1.0f);
		pbrData->FeatureScalar = pbrMaterial->GetSubsurfaceOpacity();
	}
	else if (pbrFlags.any(PBRFlags::TwoLayer)) {
		const auto& coatColor = pbrMaterial->GetSubsurfaceColor();
		pbrData->FeatureColor = float4(coatColor.red, coatColor.green, coatColor.blue, pbrMaterial->GetSubsurfaceOpacity());
		pbrData->FeatureScalar = pbrMaterial->coatRoughness;
	}
	else if (pbrFlags.any(PBRFlags::Fuzz)) {
		pbrData->FeatureColor = float4(pbrMaterial->fuzzColor.red, pbrMaterial->fuzzColor.green, pbrMaterial->fuzzColor.blue, pbrMaterial->fuzzWeight);
	}

	if (pbrMaterial->glintParameters.enabled) {
		const auto& glint = pbrMaterial->GetGlintParameters();
		pbrData->GlintParameters = float4(glint.screenSpaceScale, glint.logMicrofacetDensity, glint.microfacetRoughness, glint.densityRandomization);
	}

	UpdateTextures(shaderMaterial);

	pbrData->RMAOSTexture = m_RMAOSTexture.GetDescriptorIndex();
	pbrData->EmissiveTexture = m_EmissiveTexture.GetDescriptorIndex();
	pbrData->FeaturesTexture0 = m_FeaturesTexture0.GetDescriptorIndex();
	pbrData->FeaturesTexture1 = m_FeaturesTexture1.GetDescriptorIndex();
}

void PBRMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	m_RMAOSTexture = MaterialManager::GetTexture(pbrMaterial->rmaosTexture, renderer->GetRMAOSTextureDescriptor());
	m_EmissiveTexture = MaterialManager::GetTexture(pbrMaterial->emissiveTexture, renderer->GetBlackTextureDescriptor());
	m_FeaturesTexture0 = MaterialManager::GetTexture(pbrMaterial->featuresTexture0, renderer->GetWhiteTextureDescriptor());
	m_FeaturesTexture1 = MaterialManager::GetTexture(pbrMaterial->featuresTexture1, renderer->GetWhiteTextureDescriptor());
}
