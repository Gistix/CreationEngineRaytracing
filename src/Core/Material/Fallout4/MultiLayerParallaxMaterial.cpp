#if defined(FALLOUT4)

#include "Core/Material/Fallout4/MultiLayerParallaxMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

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
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialMultiLayerParallax*>(shaderMaterial);
    data->LayerThickness = mat->parallaxLayerThickness;
    data->RefractionScale = mat->parallaxRefractionScale;
    data->InnerLayerUScale = mat->parallaxInnerLayerUScale;
    data->InnerLayerVScale = mat->parallaxInnerLayerVScale;
    data->EnvironmentScale = mat->envmapScale;
}

void MultiLayerParallaxMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialMultiLayerParallax*>(shaderMaterial);

	if (m_LayerTexture.Update(mat->layerTexture.get(), renderer->GetBlackTextureDescriptor()))
		data->LayerTexture = m_LayerTexture.texture.GetDescriptorIndex();
	if (m_EnvironmentTexture.Update(mat->envTexture.get(), renderer->GetBlackTextureDescriptor(), TextureType::CubeMap))
		data->EnvironmentTexture = m_EnvironmentTexture.texture.GetDescriptorIndex();
	if (m_EnvironmentMaskTexture.Update(mat->envMaskTexture.get(), renderer->GetWhiteTextureDescriptor()))
		data->EnvironmentMaskTexture = m_EnvironmentMaskTexture.texture.GetDescriptorIndex();
}

#endif
