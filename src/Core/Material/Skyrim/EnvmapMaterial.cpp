#include "Core/Material/Skyrim/EnvmapMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

EnvmapMaterial::EnvmapMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<EnvmapMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void EnvmapMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);

	auto envMaterial = reinterpret_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial);

	auto envData = reinterpret_cast<Data*>(m_Data.get());
	envData->EnvironmentScale = envMaterial->envMapScale;
}

void EnvmapMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto envMaterial = reinterpret_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	auto envData = reinterpret_cast<Data*>(m_Data.get());

	if (m_EnvironmentTexture.Update(envMaterial->envTexture, renderer->GetBlackTextureDescriptor(), TextureType::CubeMap))
		envData->EnvironmentTexture = m_EnvironmentTexture.texture.GetDescriptorIndex();

	if (m_EnvironmentMaskTexture.Update(envMaterial->envMaskTexture, renderer->GetWhiteTextureDescriptor()))
		envData->EnvironmentMaskTexture = m_EnvironmentMaskTexture.texture.GetDescriptorIndex();
}
