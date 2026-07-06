#include "Core/Material/Skyrim/ParallaxOccMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

ParallaxOccMaterial::ParallaxOccMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<ParallaxOccMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void ParallaxOccMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto parallaxData = reinterpret_cast<Data*>(data);

	auto parallaxMaterial = skyrim_cast<RE::BSLightingShaderMaterialParallaxOcc*>(shaderMaterial);
	if (!parallaxMaterial) {
		logger::error("ParallaxOccMaterial::Initialize - Shader material is not BSLightingShaderMaterialParallaxOcc");
		return;
	}

	parallaxData->MaxPasses = parallaxMaterial->parallaxOccMaxPasses;
	parallaxData->Scale = parallaxMaterial->parallaxOccScale;

	parallaxData->HeightTexture = m_HeightTexture.GetDescriptorIndex();
}

void ParallaxOccMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto parallaxMaterial = skyrim_cast<RE::BSLightingShaderMaterialParallaxOcc*>(shaderMaterial);
	if (!parallaxMaterial) {
		logger::error("ParallaxOccMaterial::UpdateTextures - Shader material is not BSLightingShaderMaterialParallaxOcc");
		return;
	}

	m_HeightTexture = MaterialManager::GetTexture(parallaxMaterial->heightTexture, Renderer::GetSingleton()->GetWhiteTextureDescriptor());
}
