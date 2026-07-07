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

	auto lightingData = reinterpret_cast<Data*>(m_Data.get());

	if (m_DiffuseTexture.Update(lightingShaderMaterial->diffuseTexture, renderer->GetGrayTextureDescriptor()))
		lightingData->DiffuseTexture = m_DiffuseTexture.texture.GetDescriptorIndex();

	if (m_NormalTexture.Update(lightingShaderMaterial->normalTexture, renderer->GetNormalTextureDescriptor()))
		lightingData->NormalTexture = m_NormalTexture.texture.GetDescriptorIndex();

	if (m_RimSoftLightingTexture.Update(lightingShaderMaterial->rimSoftLightingTexture, renderer->GetBlackTextureDescriptor()))
		lightingData->RimSoftLightingTexture = m_RimSoftLightingTexture.texture.GetDescriptorIndex();

	if (m_SpecularBackLightingTexture.Update(lightingShaderMaterial->specularBackLightingTexture, renderer->GetBlackTextureDescriptor()))
		lightingData->SpecularBackLightingTexture = m_SpecularBackLightingTexture.texture.GetDescriptorIndex();
}