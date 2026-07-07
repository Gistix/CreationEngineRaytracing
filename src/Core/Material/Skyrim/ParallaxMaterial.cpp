#include "Core/Material/Skyrim/ParallaxMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

ParallaxMaterial::ParallaxMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<ParallaxMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void ParallaxMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
}

void ParallaxMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto parallaxMaterial = reinterpret_cast<RE::BSLightingShaderMaterialParallax*>(shaderMaterial);

	m_HeightTexture = MaterialManager::GetTexture(parallaxMaterial->heightTexture, Renderer::GetSingleton()->GetWhiteTextureDescriptor());

	auto parallaxData = reinterpret_cast<Data*>(m_Data.get());
	parallaxData->HeightTexture = m_HeightTexture.GetDescriptorIndex();
}
