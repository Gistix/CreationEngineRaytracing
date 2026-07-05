#include "Core/Material/Skyrim/ParallaxMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

ParallaxMaterial::ParallaxMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;

	m_Data = eastl::make_unique<ParallaxMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void ParallaxMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto parallaxData = reinterpret_cast<Data*>(data);

	parallaxData->HeightTexture = m_HeightTexture.GetDescriptorIndex();
}

void ParallaxMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto parallaxMaterial = skyrim_cast<RE::BSLightingShaderMaterialParallax*>(shaderMaterial);
	if (!parallaxMaterial) {
		logger::error("ParallaxMaterial::UpdateTextures - Shader material is not BSLightingShaderMaterialParallax");
		return;
	}

	m_HeightTexture = MaterialManager::GetTexture(parallaxMaterial->heightTexture, Renderer::GetSingleton()->GetWhiteTextureDescriptor());
}
