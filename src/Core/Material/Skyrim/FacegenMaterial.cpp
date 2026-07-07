#include "Core/Material/Skyrim/FacegenMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

FacegenMaterial::FacegenMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<FacegenMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void FacegenMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
}

void FacegenMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto facegenMaterial = reinterpret_cast<RE::BSLightingShaderMaterialFacegen*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	m_TintTexture = MaterialManager::GetTexture(facegenMaterial->tintTexture, renderer->GetWhiteTextureDescriptor());
	m_DetailTexture = MaterialManager::GetTexture(facegenMaterial->detailTexture, renderer->GetDetailTextureDescriptor());
	m_SubsurfaceTexture = MaterialManager::GetTexture(facegenMaterial->subsurfaceTexture, renderer->GetBlackTextureDescriptor());

	auto facegenData = reinterpret_cast<Data*>(m_Data.get());
	facegenData->TintTexture = m_TintTexture.GetDescriptorIndex();
	facegenData->DetailTexture = m_DetailTexture.GetDescriptorIndex();
	facegenData->SubsurfaceTexture = m_SubsurfaceTexture.GetDescriptorIndex();
}
