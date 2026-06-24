#include "Core/Material/Skyrim/LightingMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"

LightingMaterial::LightingMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;

	m_Data = eastl::make_unique<LightingMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void LightingMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	MaterialBase::Initialize(data, shaderMaterial);

	auto lightingData = reinterpret_cast<Data*>(data);

	auto lightingShaderMaterial = skyrim_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial);
	if (!lightingShaderMaterial) {
		logger::error("LightingMaterial::Initialize - Shader material is not BSLightingShaderMaterialBase");
		return;
	}

	UpdateTextures(shaderMaterial);

	lightingData->SpecularColor = Util::Math::Float3(lightingShaderMaterial->specularColor) * lightingShaderMaterial->specularColorScale;
	lightingData->SpecularPower = lightingShaderMaterial->specularPower;

	lightingData->DiffuseTexture = m_DiffuseTexture.GetDescriptorIndex();
	lightingData->NormalTexture = m_NormalTexture.GetDescriptorIndex();
	lightingData->RimSoftLightingTexture = m_RimSoftLightingTexture.GetDescriptorIndex();
	lightingData->SpecularBackLightingTexture = m_SpecularBackLightingTexture.GetDescriptorIndex();
}

void LightingMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto lightingShaderMaterial = skyrim_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial);
	if (!lightingShaderMaterial) {
		logger::error("LightingMaterial::UpdateTextures - Shader material is not BSLightingShaderMaterialBase");
		return;
	}

	auto renderer = Renderer::GetSingleton();

	m_DiffuseTexture = MaterialManager::GetTexture(lightingShaderMaterial->diffuseTexture, renderer->GetGrayTextureDescriptor());
	m_NormalTexture = MaterialManager::GetTexture(lightingShaderMaterial->normalTexture, renderer->GetNormalTextureDescriptor());
	m_RimSoftLightingTexture = MaterialManager::GetTexture(lightingShaderMaterial->rimSoftLightingTexture, renderer->GetBlackTextureDescriptor());
	m_SpecularBackLightingTexture = MaterialManager::GetTexture(lightingShaderMaterial->specularBackLightingTexture, renderer->GetBlackTextureDescriptor());
}