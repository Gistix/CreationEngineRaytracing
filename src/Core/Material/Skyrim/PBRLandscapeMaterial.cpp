#include "Core/Material/Skyrim/PBRLandscapeMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Types/CommunityShaders/BSLightingShaderMaterialPBRLandscape.h"

PBRLandscapeMaterial::PBRLandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;

	m_Data = eastl::make_unique<PBRLandscapeMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void PBRLandscapeMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto landData = reinterpret_cast<Data*>(data);

	// GetType() reports kLighting and GetFeature() reports kDefault for PBR landscape; tag it explicitly.
	landData->Type = MaterialBase::Type::TruePBR;
	landData->Feature = static_cast<uint16_t>(RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend);
	landData->PBRFlags = 0;

	UpdateTextures(shaderMaterial);

	landData->BaseColorTexture0 = m_BaseColorTextures[0].GetDescriptorIndex();
	landData->BaseColorTexture1 = m_BaseColorTextures[1].GetDescriptorIndex();
	landData->BaseColorTexture2 = m_BaseColorTextures[2].GetDescriptorIndex();
	landData->BaseColorTexture3 = m_BaseColorTextures[3].GetDescriptorIndex();
	landData->BaseColorTexture4 = m_BaseColorTextures[4].GetDescriptorIndex();
	landData->BaseColorTexture5 = m_BaseColorTextures[5].GetDescriptorIndex();

	landData->NormalTexture0 = m_NormalTextures[0].GetDescriptorIndex();
	landData->NormalTexture1 = m_NormalTextures[1].GetDescriptorIndex();
	landData->NormalTexture2 = m_NormalTextures[2].GetDescriptorIndex();
	landData->NormalTexture3 = m_NormalTextures[3].GetDescriptorIndex();
	landData->NormalTexture4 = m_NormalTextures[4].GetDescriptorIndex();
	landData->NormalTexture5 = m_NormalTextures[5].GetDescriptorIndex();

	landData->RMAOSTexture0 = m_RMAOSTextures[0].GetDescriptorIndex();
	landData->RMAOSTexture1 = m_RMAOSTextures[1].GetDescriptorIndex();
	landData->RMAOSTexture2 = m_RMAOSTextures[2].GetDescriptorIndex();
	landData->RMAOSTexture3 = m_RMAOSTextures[3].GetDescriptorIndex();
	landData->RMAOSTexture4 = m_RMAOSTextures[4].GetDescriptorIndex();
	landData->RMAOSTexture5 = m_RMAOSTextures[5].GetDescriptorIndex();

	landData->OverlayTexture = m_OverlayTexture.GetDescriptorIndex();
	landData->NoiseTexture = m_NoiseTexture.GetDescriptorIndex();

	auto landMaterial = static_cast<BSLightingShaderMaterialPBRLandscape*>(shaderMaterial);

	landData->RoughnessScale0 = landMaterial->roughnessScales[0];
	landData->RoughnessScale1 = landMaterial->roughnessScales[1];
	landData->RoughnessScale2 = landMaterial->roughnessScales[2];
	landData->RoughnessScale3 = landMaterial->roughnessScales[3];
	landData->RoughnessScale4 = landMaterial->roughnessScales[4];
	landData->RoughnessScale5 = landMaterial->roughnessScales[5];

	landData->DisplacementScale0 = landMaterial->displacementScales[0];
	landData->DisplacementScale1 = landMaterial->displacementScales[1];
	landData->DisplacementScale2 = landMaterial->displacementScales[2];
	landData->DisplacementScale3 = landMaterial->displacementScales[3];
	landData->DisplacementScale4 = landMaterial->displacementScales[4];
	landData->DisplacementScale5 = landMaterial->displacementScales[5];

	landData->SpecularLevel0 = landMaterial->specularLevels[0];
	landData->SpecularLevel1 = landMaterial->specularLevels[1];
	landData->SpecularLevel2 = landMaterial->specularLevels[2];
	landData->SpecularLevel3 = landMaterial->specularLevels[3];
	landData->SpecularLevel4 = landMaterial->specularLevels[4];
	landData->SpecularLevel5 = landMaterial->specularLevels[5];
}

void PBRLandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto landMaterial = static_cast<BSLightingShaderMaterialPBRLandscape*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	for (uint32_t i = 0; i < 6; i++) {
		m_BaseColorTextures[i] = MaterialManager::GetTexture(landMaterial->landscapeBaseColorTextures[i], renderer->GetGrayTextureDescriptor());
		m_NormalTextures[i] = MaterialManager::GetTexture(landMaterial->landscapeNormalTextures[i], renderer->GetNormalTextureDescriptor());
		m_RMAOSTextures[i] = MaterialManager::GetTexture(landMaterial->landscapeRMAOSTextures[i], renderer->GetRMAOSTextureDescriptor());
	}

	m_OverlayTexture = MaterialManager::GetTexture(landMaterial->terrainOverlayTexture, renderer->GetBlackTextureDescriptor());
	m_NoiseTexture = MaterialManager::GetTexture(landMaterial->terrainNoiseTexture, renderer->GetBlackTextureDescriptor());
}
