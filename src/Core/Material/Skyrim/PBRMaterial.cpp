#include "Core/Material/Skyrim/PBRMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
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

	pbrData->PBRFlags = static_cast<uint16_t>(pbrMaterial->pbrFlags.underlying());
	pbrData->RoughnessScale = pbrMaterial->GetRoughnessScale();
	pbrData->SpecularLevel = pbrMaterial->GetSpecularLevel();

	UpdateTextures(shaderMaterial);

	pbrData->RMAOSTexture = m_RMAOSTexture.GetDescriptorIndex();
	pbrData->EmissiveTexture = m_EmissiveTexture.GetDescriptorIndex();
}

void PBRMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	m_RMAOSTexture = MaterialManager::GetTexture(pbrMaterial->rmaosTexture, renderer->GetRMAOSTextureDescriptor());
	m_EmissiveTexture = MaterialManager::GetTexture(pbrMaterial->emissiveTexture, renderer->GetBlackTextureDescriptor());
}
