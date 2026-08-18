#if defined(FALLOUT4)

#include "Core/Material/Fallout4/ParallaxOccMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

ParallaxOccMaterial::ParallaxOccMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<ParallaxOccMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void ParallaxOccMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
}

void ParallaxOccMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialParallax*>(shaderMaterial);
	if (m_HeightTexture.Update(mat->heightTexture.get(), renderer->GetBlackTextureDescriptor()))
		data->HeightTexture = m_HeightTexture.texture.GetDescriptorIndex();
}

#endif
