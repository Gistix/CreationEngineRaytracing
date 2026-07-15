#include "Core/Material/Skyrim/EffectMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"

EffectMaterial::EffectMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<Data>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void EffectMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	auto effectData = reinterpret_cast<Data*>(m_Data.get());

	effectData->Type = MaterialBase::Type::Effect;
	effectData->Feature = static_cast<uint16_t>(shaderMaterial->GetFeature());

	effectData->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	effectData->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);

	auto effectMaterial = reinterpret_cast<RE::BSEffectShaderMaterial*>(shaderMaterial);

	effectData->BaseColor = float4(effectMaterial->baseColor.red, effectMaterial->baseColor.green,
	                               effectMaterial->baseColor.blue, effectMaterial->baseColor.alpha);
	effectData->BaseColorScale = effectMaterial->baseColorScale;
}

void EffectMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto effectMaterial = reinterpret_cast<RE::BSEffectShaderMaterial*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	auto effectData = reinterpret_cast<Data*>(m_Data.get());

	if (m_SourceTexture.Update(effectMaterial->sourceTexture, renderer->GetBlackTextureDescriptor()))
		effectData->SourceTexture = m_SourceTexture.texture.GetDescriptorIndex();

	if (m_EffectTexture.Update(effectMaterial->greyscaleTexture, renderer->GetBlackTextureDescriptor()))
		effectData->EffectTexture = m_EffectTexture.texture.GetDescriptorIndex();
}
