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

	auto facegenData = reinterpret_cast<Data*>(m_Data.get());

	if (m_TintTexture.Update(facegenMaterial->tintTexture, renderer->GetWhiteTextureDescriptor()))
		facegenData->TintTexture = m_TintTexture.texture.GetDescriptorIndex();

	if (m_DetailTexture.Update(facegenMaterial->detailTexture, renderer->GetDetailTextureDescriptor()))
		facegenData->DetailTexture = m_DetailTexture.texture.GetDescriptorIndex();

	if (m_SubsurfaceTexture.Update(facegenMaterial->subsurfaceTexture, renderer->GetBlackTextureDescriptor()))
		facegenData->SubsurfaceTexture = m_SubsurfaceTexture.texture.GetDescriptorIndex();
}
