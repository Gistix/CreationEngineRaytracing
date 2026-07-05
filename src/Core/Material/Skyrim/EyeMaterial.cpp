#include "Core/Material/Skyrim/EyeMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

EyeMaterial::EyeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;

	m_Data = eastl::make_unique<EyeMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void EyeMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto eyeData = reinterpret_cast<Data*>(data);

	auto eyeMaterial = skyrim_cast<RE::BSLightingShaderMaterialEye*>(shaderMaterial);
	if (!eyeMaterial) {
		logger::error("EyeMaterial::Initialize - Shader material is not BSLightingShaderMaterialEye");
		return;
	}

	eyeData->EnvironmentScale = eyeMaterial->envMapScale;

	eyeData->EnvironmentTexture = m_EnvironmentTexture.GetDescriptorIndex();
	eyeData->EnvironmentMaskTexture = m_EnvironmentMaskTexture.GetDescriptorIndex();
}

void EyeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto eyeMaterial = skyrim_cast<RE::BSLightingShaderMaterialEye*>(shaderMaterial);
	if (!eyeMaterial) {
		logger::error("EyeMaterial::UpdateTextures - Shader material is not BSLightingShaderMaterialEye");
		return;
	}

	auto renderer = Renderer::GetSingleton();

	m_EnvironmentTexture = MaterialManager::GetTexture(eyeMaterial->envTexture, renderer->GetBlackTextureDescriptor(), TextureType::CubeMap);
	m_EnvironmentMaskTexture = MaterialManager::GetTexture(eyeMaterial->envMaskTexture, renderer->GetWhiteTextureDescriptor());
}
