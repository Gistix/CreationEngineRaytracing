#include "Core/Material/Skyrim/LandscapeMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

LandscapeMaterial::LandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<LandscapeMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void LandscapeMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
}

void LandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto landMaterial = reinterpret_cast<RE::BSLightingShaderMaterialLandscape*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	for (uint32_t i = 0; i < 5; i++) {
		m_DiffuseTextures[i] = MaterialManager::GetTexture(landMaterial->landscapeDiffuseTexture[i], renderer->GetGrayTextureDescriptor());
		m_NormalTextures[i] = MaterialManager::GetTexture(landMaterial->landscapeNormalTexture[i], renderer->GetNormalTextureDescriptor());
	}

	m_OverlayTexture = MaterialManager::GetTexture(landMaterial->terrainOverlayTexture, renderer->GetBlackTextureDescriptor());
	m_NoiseTexture = MaterialManager::GetTexture(landMaterial->terrainNoiseTexture, renderer->GetBlackTextureDescriptor());

	auto landData = reinterpret_cast<Data*>(m_Data.get());
	landData->DiffuseTexture1 = m_DiffuseTextures[0].GetDescriptorIndex();
	landData->DiffuseTexture2 = m_DiffuseTextures[1].GetDescriptorIndex();
	landData->DiffuseTexture3 = m_DiffuseTextures[2].GetDescriptorIndex();
	landData->DiffuseTexture4 = m_DiffuseTextures[3].GetDescriptorIndex();
	landData->DiffuseTexture5 = m_DiffuseTextures[4].GetDescriptorIndex();

	landData->NormalTexture1 = m_NormalTextures[0].GetDescriptorIndex();
	landData->NormalTexture2 = m_NormalTextures[1].GetDescriptorIndex();
	landData->NormalTexture3 = m_NormalTextures[2].GetDescriptorIndex();
	landData->NormalTexture4 = m_NormalTextures[3].GetDescriptorIndex();
	landData->NormalTexture5 = m_NormalTextures[4].GetDescriptorIndex();

	landData->OverlayTexture = m_OverlayTexture.GetDescriptorIndex();
	landData->NoiseTexture = m_NoiseTexture.GetDescriptorIndex();
}
