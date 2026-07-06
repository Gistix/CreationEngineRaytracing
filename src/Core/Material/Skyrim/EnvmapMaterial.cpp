#include "Core/Material/Skyrim/EnvmapMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

EnvmapMaterial::EnvmapMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<EnvmapMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void EnvmapMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto envData = reinterpret_cast<Data*>(data);

	auto envMaterial = skyrim_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial);
	if (!envMaterial) {
		logger::error("EnvmapMaterial::Initialize - Shader material is not BSLightingShaderMaterialEnvmap");
		return;
	}

	envData->EnvironmentScale = envMaterial->envMapScale;

	envData->EnvironmentTexture = m_EnvironmentTexture.GetDescriptorIndex();
	envData->EnvironmentMaskTexture = m_EnvironmentMaskTexture.GetDescriptorIndex();
}

void EnvmapMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto envMaterial = skyrim_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial);
	if (!envMaterial) {
		logger::error("EnvmapMaterial::UpdateTextures - Shader material is not BSLightingShaderMaterialEnvmap");
		return;
	}

	auto renderer = Renderer::GetSingleton();

	m_EnvironmentTexture = MaterialManager::GetTexture(envMaterial->envTexture, renderer->GetBlackTextureDescriptor(), TextureType::CubeMap);
	m_EnvironmentMaskTexture = MaterialManager::GetTexture(envMaterial->envMaskTexture, renderer->GetWhiteTextureDescriptor());
}
