#include "Core/Material/Skyrim/ParallaxOccMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

ParallaxOccMaterial::ParallaxOccMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<ParallaxOccMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void ParallaxOccMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);

	auto parallaxMaterial = reinterpret_cast<RE::BSLightingShaderMaterialParallaxOcc*>(shaderMaterial);

	auto parallaxData = reinterpret_cast<Data*>(m_Data.get());
	parallaxData->MaxPasses = parallaxMaterial->parallaxOccMaxPasses;
	parallaxData->Scale = parallaxMaterial->parallaxOccScale;
}

void ParallaxOccMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto parallaxMaterial = reinterpret_cast<RE::BSLightingShaderMaterialParallaxOcc*>(shaderMaterial);

	auto parallaxData = reinterpret_cast<Data*>(m_Data.get());

	if (m_HeightTexture.Update(parallaxMaterial->heightTexture, Renderer::GetSingleton()->GetWhiteTextureDescriptor()))
		parallaxData->HeightTexture = m_HeightTexture.texture.GetDescriptorIndex();
}
