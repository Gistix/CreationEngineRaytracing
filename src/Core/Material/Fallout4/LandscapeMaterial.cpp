#if defined(FALLOUT4)

#include "Core/Material/Fallout4/LandscapeMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

LandscapeMaterial::LandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<LandscapeMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void LandscapeMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
	auto* material = static_cast<RE::BSLightingShaderMaterialLandscape*>(shaderMaterial);
	reinterpret_cast<Data*>(m_Data.get())->NumTextures = static_cast<uint16_t>(material->textureCount);
}

void LandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialLandscape*>(shaderMaterial);
    
    uint32_t landscapeTextureCount = std::min(mat->textureCount, 3u);

	for (uint32_t i = 0; i < landscapeTextureCount; ++i) {
		if (m_DiffuseTextures[i].Update(mat->landscapeDiffuseTexture[i].get(), renderer->GetGrayTextureDescriptor()))
			(&data->DiffuseTexture1)[i] = m_DiffuseTextures[i].texture.GetDescriptorIndex();

		if (m_NormalTextures[i].Update(mat->landscapeNormalTexture[i].get(), renderer->GetNormalTextureDescriptor()))
			(&data->NormalTexture1)[i] = m_NormalTextures[i].texture.GetDescriptorIndex();

		if (m_SmoothSpecTextures[i].Update(mat->landscapeSpecularTexture[i].get(), renderer->GetBlackTextureDescriptor()))
			(&data->SmoothSpecTexture1)[i] = m_SmoothSpecTextures[i].texture.GetDescriptorIndex();
	}

	if (m_OverlayTexture.Update(mat->terrainOverlayTexture.get(), renderer->GetBlackTextureDescriptor()))
		data->OverlayTexture = m_OverlayTexture.texture.GetDescriptorIndex();

	if (m_NoiseTexture.Update(mat->terrainNoiseTexture.get(), renderer->GetBlackTextureDescriptor()))
		data->NoiseTexture = m_NoiseTexture.texture.GetDescriptorIndex();
}

#endif
