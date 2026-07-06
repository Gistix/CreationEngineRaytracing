#include "Core/Material/Skyrim/MultiLayerParallaxMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

MultiLayerParallaxMaterial::MultiLayerParallaxMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<MultiLayerParallaxMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void MultiLayerParallaxMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto layerData = reinterpret_cast<Data*>(data);

	auto layerMaterial = skyrim_cast<RE::BSLightingShaderMaterialMultiLayerParallax*>(shaderMaterial);
	if (!layerMaterial) {
		logger::error("MultiLayerParallaxMaterial::Initialize - Shader material is not BSLightingShaderMaterialMultiLayerParallax");
		return;
	}

	layerData->LayerThickness = layerMaterial->parallaxLayerThickness;
	layerData->RefractionScale = layerMaterial->parallaxRefractionScale;
	layerData->InnerLayerUScale = layerMaterial->parallaxInnerLayerUScale;
	layerData->InnerLayerVScale = layerMaterial->parallaxInnerLayerVScale;
	layerData->EnvironmentScale = layerMaterial->envmapScale;

	layerData->LayerTexture = m_LayerTexture.GetDescriptorIndex();
	layerData->EnvironmentTexture = m_EnvironmentTexture.GetDescriptorIndex();
	layerData->EnvironmentMaskTexture = m_EnvironmentMaskTexture.GetDescriptorIndex();
}

void MultiLayerParallaxMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto layerMaterial = skyrim_cast<RE::BSLightingShaderMaterialMultiLayerParallax*>(shaderMaterial);
	if (!layerMaterial) {
		logger::error("MultiLayerParallaxMaterial::UpdateTextures - Shader material is not BSLightingShaderMaterialMultiLayerParallax");
		return;
	}

	auto renderer = Renderer::GetSingleton();

	m_LayerTexture = MaterialManager::GetTexture(layerMaterial->layerTexture, renderer->GetBlackTextureDescriptor());
	m_EnvironmentTexture = MaterialManager::GetTexture(layerMaterial->envTexture, renderer->GetBlackTextureDescriptor(), TextureType::CubeMap);
	m_EnvironmentMaskTexture = MaterialManager::GetTexture(layerMaterial->envMaskTexture, renderer->GetWhiteTextureDescriptor());
}
