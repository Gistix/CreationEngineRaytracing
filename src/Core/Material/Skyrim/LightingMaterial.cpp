#include "Core/Material/Skyrim/LightingMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"

LightingMaterial::LightingMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<LightingMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void LightingMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	MaterialBase::UpdateData(shaderMaterial);

	auto lightingShaderMaterial = reinterpret_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial);

	auto lightingData = reinterpret_cast<Data*>(m_Data.get());

	lightingData->SpecularColor = Util::Math::Float3(lightingShaderMaterial->specularColor);
	lightingData->SpecularColorScale = lightingShaderMaterial->specularColorScale;
	lightingData->SpecularPower = lightingShaderMaterial->specularPower;
	lightingData->MaterialAlpha = lightingShaderMaterial->materialAlpha;
}

void LightingMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto lightingShaderMaterial = reinterpret_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	m_DiffuseTexture = MaterialManager::GetTexture(lightingShaderMaterial->diffuseTexture, renderer->GetGrayTextureDescriptor());
	m_NormalTexture = MaterialManager::GetTexture(lightingShaderMaterial->normalTexture, renderer->GetNormalTextureDescriptor());
	m_RimSoftLightingTexture = MaterialManager::GetTexture(lightingShaderMaterial->rimSoftLightingTexture, renderer->GetBlackTextureDescriptor());
	m_SpecularBackLightingTexture = MaterialManager::GetTexture(lightingShaderMaterial->specularBackLightingTexture, renderer->GetBlackTextureDescriptor());

	auto lightingData = reinterpret_cast<Data*>(m_Data.get());
	lightingData->DiffuseTexture = m_DiffuseTexture.GetDescriptorIndex();
	lightingData->NormalTexture = m_NormalTexture.GetDescriptorIndex();
	lightingData->RimSoftLightingTexture = m_RimSoftLightingTexture.GetDescriptorIndex();
	lightingData->SpecularBackLightingTexture = m_SpecularBackLightingTexture.GetDescriptorIndex();
}