#if defined(FALLOUT4)

#include "Core/Material/Skyrim/EffectMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

EffectMaterial::EffectMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<EffectMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void EffectMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	MaterialBase::UpdateData(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	data->Type = MaterialBase::Type::Effect;
	data->BaseColor = runtime.baseColor;
	data->BaseColorScale = runtime.baseColorScale;
}

void EffectMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	const auto renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	if (m_SourceTexture.Update(runtime.sourceTexture, renderer->GetBlackTextureDescriptor()))
		data->SourceTexture = m_SourceTexture.texture.GetDescriptorIndex();
	if (m_EffectTexture.Update(runtime.effectTexture, renderer->GetBlackTextureDescriptor()))
		data->EffectTexture = m_EffectTexture.texture.GetDescriptorIndex();
}

#endif
