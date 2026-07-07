#include "Core/Material/Skyrim/PBRMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"
#include "Utils/Material.h"
#include "Types/CommunityShaders/BSLightingShaderMaterialPBR.h"

PBRMaterial::PBRMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<Data>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void PBRMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	auto pbrData = reinterpret_cast<Data*>(m_Data.get());

	pbrData->Type = MaterialBase::Type::TruePBR;
	pbrData->Feature = static_cast<uint16_t>(shaderMaterial->GetFeature());

	pbrData->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	pbrData->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);

	auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

	pbrData->SpecularColor = Util::Math::Float3(pbrMaterial->specularColor);
	pbrData->SpecularLevel = pbrMaterial->GetSpecularLevel();
	pbrData->RoughnessScale = pbrMaterial->GetRoughnessScale();
	pbrData->MaterialAlpha = pbrMaterial->materialAlpha;

	pbrData->PBRFlags = static_cast<uint16_t>(Util::Material::Skyrim::GetPBRShaderFlags(pbrMaterial).underlying());

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
}

void PBRMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	m_DiffuseTexture = MaterialManager::GetTexture(pbrMaterial->diffuseTexture, renderer->GetGrayTextureDescriptor());
	m_NormalTexture = MaterialManager::GetTexture(pbrMaterial->normalTexture, renderer->GetNormalTextureDescriptor());
	m_RimSoftLightingTexture = MaterialManager::GetTexture(pbrMaterial->rimSoftLightingTexture, renderer->GetBlackTextureDescriptor());
	m_SpecularBackLightingTexture = MaterialManager::GetTexture(pbrMaterial->specularBackLightingTexture, renderer->GetBlackTextureDescriptor());
	m_RMAOSTexture = MaterialManager::GetTexture(pbrMaterial->rmaosTexture, renderer->GetRMAOSTextureDescriptor());
	m_EmissiveTexture = MaterialManager::GetTexture(pbrMaterial->emissiveTexture, renderer->GetBlackTextureDescriptor());
	m_FeaturesTexture0 = MaterialManager::GetTexture(pbrMaterial->featuresTexture0, renderer->GetWhiteTextureDescriptor());
	m_FeaturesTexture1 = MaterialManager::GetTexture(pbrMaterial->featuresTexture1, renderer->GetWhiteTextureDescriptor());

	auto pbrData = reinterpret_cast<Data*>(m_Data.get());
	pbrData->DiffuseTexture = m_DiffuseTexture.GetDescriptorIndex();
	pbrData->NormalTexture = m_NormalTexture.GetDescriptorIndex();
	pbrData->RimSoftLightingTexture = m_RimSoftLightingTexture.GetDescriptorIndex();
	pbrData->SpecularBackLightingTexture = m_SpecularBackLightingTexture.GetDescriptorIndex();
	pbrData->RMAOSTexture = m_RMAOSTexture.GetDescriptorIndex();
	pbrData->EmissiveTexture = m_EmissiveTexture.GetDescriptorIndex();
	pbrData->FeaturesTexture0 = m_FeaturesTexture0.GetDescriptorIndex();
	pbrData->FeaturesTexture1 = m_FeaturesTexture1.GetDescriptorIndex();
}
