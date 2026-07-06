#include "Core/Material/Skyrim/GlowmapMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

GlowmapMaterial::GlowmapMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<GlowmapMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void GlowmapMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto glowData = reinterpret_cast<Data*>(data);

	glowData->GlowTexture = m_GlowTexture.GetDescriptorIndex();
}

void GlowmapMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto glowMaterial = skyrim_cast<RE::BSLightingShaderMaterialGlowmap*>(shaderMaterial);
	if (!glowMaterial) {
		logger::error("GlowmapMaterial::UpdateTextures - Shader material is not BSLightingShaderMaterialGlowmap");
		return;
	}

	m_GlowTexture = MaterialManager::GetTexture(glowMaterial->glowTexture, Renderer::GetSingleton()->GetBlackTextureDescriptor());
}
