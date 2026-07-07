#include "Core/Material/Skyrim/MultiLayerParallaxMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

MultiLayerParallaxMaterial::MultiLayerParallaxMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<MultiLayerParallaxMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void MultiLayerParallaxMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);

	auto layerMaterial = reinterpret_cast<RE::BSLightingShaderMaterialMultiLayerParallax*>(shaderMaterial);

	auto layerData = reinterpret_cast<Data*>(m_Data.get());
	layerData->LayerThickness = layerMaterial->parallaxLayerThickness;
	layerData->RefractionScale = layerMaterial->parallaxRefractionScale;
	layerData->InnerLayerUScale = layerMaterial->parallaxInnerLayerUScale;
	layerData->InnerLayerVScale = layerMaterial->parallaxInnerLayerVScale;
	layerData->EnvironmentScale = layerMaterial->envmapScale;
}

void MultiLayerParallaxMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto layerMaterial = reinterpret_cast<RE::BSLightingShaderMaterialMultiLayerParallax*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	m_LayerTexture = MaterialManager::GetTexture(layerMaterial->layerTexture, renderer->GetBlackTextureDescriptor());
	m_EnvironmentTexture = MaterialManager::GetTexture(layerMaterial->envTexture, renderer->GetBlackTextureDescriptor(), TextureType::CubeMap);
	m_EnvironmentMaskTexture = MaterialManager::GetTexture(layerMaterial->envMaskTexture, renderer->GetWhiteTextureDescriptor());

	auto layerData = reinterpret_cast<Data*>(m_Data.get());
	layerData->LayerTexture = m_LayerTexture.GetDescriptorIndex();
	layerData->EnvironmentTexture = m_EnvironmentTexture.GetDescriptorIndex();
	layerData->EnvironmentMaskTexture = m_EnvironmentMaskTexture.GetDescriptorIndex();
}
