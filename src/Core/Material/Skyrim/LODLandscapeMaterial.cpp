#include "Core/Material/Skyrim/LODLandscapeMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

LODLandscapeMaterial::LODLandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<LODLandscapeMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void LODLandscapeMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);

	auto lodMaterial = reinterpret_cast<RE::BSLightingShaderMaterialLODLandscape*>(shaderMaterial);

	auto lodData = reinterpret_cast<Data*>(m_Data.get());
	lodData->TexOffsetX = lodMaterial->terrainTexOffsetX;
	lodData->TexOffsetY = lodMaterial->terrainTexOffsetY;
	lodData->TexFade = lodMaterial->terrainTexFade;
}

void LODLandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto lodMaterial = reinterpret_cast<RE::BSLightingShaderMaterialLODLandscape*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	m_ParentDiffuseTexture = MaterialManager::GetTexture(lodMaterial->parentDiffuseTexture, renderer->GetGrayTextureDescriptor());
	m_ParentNormalTexture = MaterialManager::GetTexture(lodMaterial->parentNormalTexture, renderer->GetNormalTextureDescriptor());
	m_NoiseTexture = MaterialManager::GetTexture(lodMaterial->landscapeNoiseTexture, renderer->GetBlackTextureDescriptor());

	auto lodData = reinterpret_cast<Data*>(m_Data.get());
	lodData->ParentDiffuseTexture = m_ParentDiffuseTexture.GetDescriptorIndex();
	lodData->ParentNormalTexture = m_ParentNormalTexture.GetDescriptorIndex();
	lodData->NoiseTexture = m_NoiseTexture.GetDescriptorIndex();
}
