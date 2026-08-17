#if defined(FALLOUT4)

#include "Core/Material/Skyrim/GlowmapMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

GlowmapMaterial::GlowmapMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<GlowmapMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void GlowmapMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
}

void GlowmapMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	if (m_GlowTexture.Update(Util::Adapter::GetMaterialRuntimeData(shaderMaterial).glowTexture, Renderer::GetSingleton()->GetBlackTextureDescriptor()))
		data->GlowTexture = m_GlowTexture.texture.GetDescriptorIndex();
}

#endif
