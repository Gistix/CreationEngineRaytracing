#if defined(FALLOUT4)

#include "Core/Material/Fallout4/GlowmapMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

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
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialGlowmap*>(shaderMaterial);
    
	if (m_GlowTexture.Update(mat->glowTexture.get(), renderer->GetBlackTextureDescriptor()))
		data->GlowTexture = m_GlowTexture.texture.GetDescriptorIndex();
}

#endif
