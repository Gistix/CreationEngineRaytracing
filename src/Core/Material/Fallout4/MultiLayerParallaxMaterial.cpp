#if defined(FALLOUT4)

#include "Core/Material/Skyrim/MultiLayerParallaxMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

MultiLayerParallaxMaterial::MultiLayerParallaxMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<MultiLayerParallaxMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void MultiLayerParallaxMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	data->LayerThickness = runtime.layerThickness;
	data->RefractionScale = runtime.refractionScale;
	data->InnerLayerUScale = runtime.innerLayerUScale;
	data->InnerLayerVScale = runtime.innerLayerVScale;
	data->EnvironmentScale = runtime.environmentScale;
}

void MultiLayerParallaxMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	if (m_LayerTexture.Update(runtime.layerTexture, renderer->GetBlackTextureDescriptor()))
		data->LayerTexture = m_LayerTexture.texture.GetDescriptorIndex();
	if (m_EnvironmentTexture.Update(runtime.environmentTexture, renderer->GetBlackTextureDescriptor(), TextureType::CubeMap))
		data->EnvironmentTexture = m_EnvironmentTexture.texture.GetDescriptorIndex();
	if (m_EnvironmentMaskTexture.Update(runtime.environmentMaskTexture, renderer->GetWhiteTextureDescriptor()))
		data->EnvironmentMaskTexture = m_EnvironmentMaskTexture.texture.GetDescriptorIndex();
}

#endif
