#include "Core/Material/Skyrim/GlowmapMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

GlowmapMaterial::GlowmapMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

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

	auto glowMaterial = reinterpret_cast<RE::BSLightingShaderMaterialGlowmap*>(shaderMaterial);

	auto glowData = reinterpret_cast<Data*>(m_Data.get());

	if (m_GlowTexture.Update(glowMaterial->glowTexture, Renderer::GetSingleton()->GetBlackTextureDescriptor()))
		glowData->GlowTexture = m_GlowTexture.texture.GetDescriptorIndex();
}
