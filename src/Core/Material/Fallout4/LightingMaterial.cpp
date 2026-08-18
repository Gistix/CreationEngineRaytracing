#if defined(FALLOUT4)

#include "Core/Material/Fallout4/LightingMaterial.h"
#include "Renderer.h"
#include "RE/B/BSLightingShaderMaterialBase.h"

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
	auto* lighting = static_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	data->SpecularColor = { lighting->specularColor.r, lighting->specularColor.g, lighting->specularColor.b };
	data->MaterialAlpha = lighting->materialAlpha;
	data->RefractionPower = lighting->refractionPower;
	data->Smoothness = lighting->smoothness;
	data->SpecularColorScale = lighting->specularColorScale;
	data->FresnelPower = lighting->fresnelPower;
	data->WetnessControl_SpecScale = lighting->wetnessControl_SpecScale;
	data->WetnessControl_SpecPowerScale = lighting->wetnessControl_SpecPowerScale;
	data->WetnessControl_SpecMin = lighting->wetnessControl_SpecMin;
	data->WetnessControl_EnvMapScale = lighting->wetnessControl_EnvMapScale;
	data->WetnessControl_FresnelPower = lighting->wetnessControl_FresnelPower;
	data->WetnessControl_Metalness = lighting->wetnessControl_Metalness;
	data->SubSurfaceLightRolloff = lighting->subSurfaceLightRolloff;
	data->RimLightPower = lighting->rimLightPower;
	data->BackLightPower = lighting->backLightPower;
	data->LookupScale = lighting->lookupScale;
}

void LightingMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto* lighting = static_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());

	if (m_DiffuseTexture.Update(lighting->diffuseTexture.get(), renderer->GetGrayTextureDescriptor()))
		data->DiffuseTexture = m_DiffuseTexture.texture.GetDescriptorIndex();
	if (m_NormalTexture.Update(lighting->normalTexture.get(), renderer->GetNormalTextureDescriptor()))
		data->NormalTexture = m_NormalTexture.texture.GetDescriptorIndex();
	if (m_RimSoftLightingTexture.Update(lighting->rimSoftLightingTexture.get(), renderer->GetBlackTextureDescriptor()))
		data->RimSoftLightingTexture = m_RimSoftLightingTexture.texture.GetDescriptorIndex();
	if (m_SmoothnessSpecMaskTexture.Update(lighting->smoothnessSpecMaskTexture.get(), renderer->GetBlackTextureDescriptor()))
		data->SmoothnessSpecMaskTexture = m_SmoothnessSpecMaskTexture.texture.GetDescriptorIndex();
	if (m_LookupTexture.Update(lighting->lookupTexture.get(), renderer->GetBlackTextureDescriptor()))
		data->LookupTexture = m_LookupTexture.texture.GetDescriptorIndex();
}

#endif
