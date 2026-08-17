#if defined(FALLOUT4)

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

LightingMaterial::LightingMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<LightingMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void LightingMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	MaterialBase::UpdateData(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	data->SpecularColor = runtime.specularColor;
	data->SpecularColorScale = runtime.specularColorScale;
	data->SpecularPower = runtime.specularPower;
	data->MaterialAlpha = runtime.materialAlpha;
}

void LightingMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());

	if (m_DiffuseTexture.Update(runtime.diffuseTexture, renderer->GetGrayTextureDescriptor()))
		data->DiffuseTexture = m_DiffuseTexture.texture.GetDescriptorIndex();
	if (m_NormalTexture.Update(runtime.normalTexture, renderer->GetNormalTextureDescriptor()))
		data->NormalTexture = m_NormalTexture.texture.GetDescriptorIndex();
	if (m_RimSoftLightingTexture.Update(runtime.rimSoftLightingTexture, renderer->GetBlackTextureDescriptor()))
		data->RimSoftLightingTexture = m_RimSoftLightingTexture.texture.GetDescriptorIndex();
	if (m_SpecularBackLightingTexture.Update(runtime.specularBackLightingTexture, renderer->GetBlackTextureDescriptor()))
		data->SpecularBackLightingTexture = m_SpecularBackLightingTexture.texture.GetDescriptorIndex();
}

#endif
