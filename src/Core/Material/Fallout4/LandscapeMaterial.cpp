#if defined(FALLOUT4)

#include "Core/Material/Skyrim/LandscapeMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

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
}

void LandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	for (uint32_t i = 0; i < runtime.landscapeTextureCount && i < 5; ++i) {
		if (m_DiffuseTextures[i].Update(runtime.landscapeDiffuseTextures[i], renderer->GetGrayTextureDescriptor()))
			(&data->DiffuseTexture1)[i] = m_DiffuseTextures[i].texture.GetDescriptorIndex();
		if (m_NormalTextures[i].Update(runtime.landscapeNormalTextures[i], renderer->GetNormalTextureDescriptor()))
			(&data->NormalTexture1)[i] = m_NormalTextures[i].texture.GetDescriptorIndex();
	}
	if (m_OverlayTexture.Update(runtime.terrainOverlayTexture, renderer->GetBlackTextureDescriptor()))
		data->OverlayTexture = m_OverlayTexture.texture.GetDescriptorIndex();
	if (m_NoiseTexture.Update(runtime.terrainNoiseTexture, renderer->GetBlackTextureDescriptor()))
		data->NoiseTexture = m_NoiseTexture.texture.GetDescriptorIndex();
}

#endif
