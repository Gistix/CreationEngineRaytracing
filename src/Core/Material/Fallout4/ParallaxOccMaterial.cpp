#if defined(FALLOUT4)

#include "Core/Material/Skyrim/ParallaxOccMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

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
	if (m_HeightTexture.Update(Util::Adapter::GetMaterialRuntimeData(shaderMaterial).heightTexture, Renderer::GetSingleton()->GetWhiteTextureDescriptor()))
		reinterpret_cast<Data*>(m_Data.get())->HeightTexture = m_HeightTexture.texture.GetDescriptorIndex();
}

#endif
