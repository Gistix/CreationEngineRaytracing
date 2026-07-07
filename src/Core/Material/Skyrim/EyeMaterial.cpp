#include "Core/Material/Skyrim/EyeMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

EyeMaterial::EyeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<EyeMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void EyeMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);

	auto eyeMaterial = reinterpret_cast<RE::BSLightingShaderMaterialEye*>(shaderMaterial);

	auto eyeData = reinterpret_cast<Data*>(m_Data.get());
	eyeData->EnvironmentScale = eyeMaterial->envMapScale;
}

void EyeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto eyeMaterial = reinterpret_cast<RE::BSLightingShaderMaterialEye*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	m_EnvironmentTexture = MaterialManager::GetTexture(eyeMaterial->envTexture, renderer->GetBlackTextureDescriptor(), TextureType::CubeMap);
	m_EnvironmentMaskTexture = MaterialManager::GetTexture(eyeMaterial->envMaskTexture, renderer->GetWhiteTextureDescriptor());

	auto eyeData = reinterpret_cast<Data*>(m_Data.get());
	eyeData->EnvironmentTexture = m_EnvironmentTexture.GetDescriptorIndex();
	eyeData->EnvironmentMaskTexture = m_EnvironmentMaskTexture.GetDescriptorIndex();
}
