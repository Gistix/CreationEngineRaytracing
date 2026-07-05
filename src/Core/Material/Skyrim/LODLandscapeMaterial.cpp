#include "Core/Material/Skyrim/LODLandscapeMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

LODLandscapeMaterial::LODLandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;

	m_Data = eastl::make_unique<LODLandscapeMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void LODLandscapeMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto lodData = reinterpret_cast<Data*>(data);

	auto lodMaterial = skyrim_cast<RE::BSLightingShaderMaterialLODLandscape*>(shaderMaterial);
	if (!lodMaterial) {
		logger::error("LODLandscapeMaterial::Initialize - Shader material is not BSLightingShaderMaterialLODLandscape");
		return;
	}

	UpdateTextures(shaderMaterial);

	lodData->TexOffsetX = lodMaterial->terrainTexOffsetX;
	lodData->TexOffsetY = lodMaterial->terrainTexOffsetY;
	lodData->TexFade = lodMaterial->terrainTexFade;

	lodData->ParentDiffuseTexture = m_ParentDiffuseTexture.GetDescriptorIndex();
	lodData->ParentNormalTexture = m_ParentNormalTexture.GetDescriptorIndex();
	lodData->NoiseTexture = m_NoiseTexture.GetDescriptorIndex();
}

void LODLandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto lodMaterial = skyrim_cast<RE::BSLightingShaderMaterialLODLandscape*>(shaderMaterial);
	if (!lodMaterial) {
		logger::error("LODLandscapeMaterial::UpdateTextures - Shader material is not BSLightingShaderMaterialLODLandscape");
		return;
	}

	auto renderer = Renderer::GetSingleton();

	m_ParentDiffuseTexture = MaterialManager::GetTexture(lodMaterial->parentDiffuseTexture, renderer->GetGrayTextureDescriptor());
	m_ParentNormalTexture = MaterialManager::GetTexture(lodMaterial->parentNormalTexture, renderer->GetNormalTextureDescriptor());
	m_NoiseTexture = MaterialManager::GetTexture(lodMaterial->landscapeNoiseTexture, renderer->GetBlackTextureDescriptor());
}
