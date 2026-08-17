#if defined(FALLOUT4)

#include "Core/Material/Skyrim/ParallaxMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

ParallaxMaterial::ParallaxMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
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
	if (m_HeightTexture.Update(Util::Adapter::GetMaterialRuntimeData(shaderMaterial).heightTexture, Renderer::GetSingleton()->GetWhiteTextureDescriptor()))
		reinterpret_cast<Data*>(m_Data.get())->HeightTexture = m_HeightTexture.texture.GetDescriptorIndex();
}

#endif
