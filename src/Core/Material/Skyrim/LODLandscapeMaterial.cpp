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

	auto lodData = reinterpret_cast<Data*>(m_Data.get());

	if (m_ParentDiffuseTexture.Update(lodMaterial->parentDiffuseTexture, renderer->GetGrayTextureDescriptor()))
		lodData->ParentDiffuseTexture = m_ParentDiffuseTexture.texture.GetDescriptorIndex();

	if (m_ParentNormalTexture.Update(lodMaterial->parentNormalTexture, renderer->GetNormalTextureDescriptor()))
		lodData->ParentNormalTexture = m_ParentNormalTexture.texture.GetDescriptorIndex();

	if (m_NoiseTexture.Update(lodMaterial->landscapeNoiseTexture, renderer->GetBlackTextureDescriptor()))
		lodData->NoiseTexture = m_NoiseTexture.texture.GetDescriptorIndex();
}
