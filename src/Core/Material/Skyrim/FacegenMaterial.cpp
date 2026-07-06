#include "Core/Material/Skyrim/FacegenMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

FacegenMaterial::FacegenMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<FacegenMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void FacegenMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto facegenData = reinterpret_cast<Data*>(data);

	facegenData->TintTexture = m_TintTexture.GetDescriptorIndex();
	facegenData->DetailTexture = m_DetailTexture.GetDescriptorIndex();
	facegenData->SubsurfaceTexture = m_SubsurfaceTexture.GetDescriptorIndex();
}

void FacegenMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto facegenMaterial = skyrim_cast<RE::BSLightingShaderMaterialFacegen*>(shaderMaterial);
	if (!facegenMaterial) {
		logger::error("FacegenMaterial::UpdateTextures - Shader material is not BSLightingShaderMaterialFacegen");
		return;
	}

	auto renderer = Renderer::GetSingleton();

	m_TintTexture = MaterialManager::GetTexture(facegenMaterial->tintTexture, renderer->GetWhiteTextureDescriptor());
	m_DetailTexture = MaterialManager::GetTexture(facegenMaterial->detailTexture, renderer->GetDetailTextureDescriptor());
	m_SubsurfaceTexture = MaterialManager::GetTexture(facegenMaterial->subsurfaceTexture, renderer->GetBlackTextureDescriptor());
}
