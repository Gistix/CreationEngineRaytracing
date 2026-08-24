#if defined(FALLOUT4)

#include "Core/Material/Fallout4/EffectMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

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
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSEffectShaderMaterial*>(shaderMaterial);
	data->Type = MaterialBase::Type::Effect;
	data->BaseColor.x = mat->baseColor.r; data->BaseColor.y = mat->baseColor.g; data->BaseColor.z = mat->baseColor.b;
	data->BaseColorScale = mat->baseColorScale;
}

void EffectMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
    auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSEffectShaderMaterial*>(shaderMaterial);

	if (m_SourceTexture.Update(mat->sourceTexture.get(), renderer->GetWhiteTextureDescriptor()))
		data->SourceTexture = m_SourceTexture.texture.GetDescriptorIndex();
	if (m_EffectTexture.Update(mat->greyscaleTexture.get(), renderer->GetWhiteTextureDescriptor()))
		data->EffectTexture = m_EffectTexture.texture.GetDescriptorIndex();
}

#endif
