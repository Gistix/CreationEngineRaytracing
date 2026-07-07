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

	m_SourceTexture = MaterialManager::GetTexture(effectMaterial->sourceTexture, renderer->GetBlackTextureDescriptor());
	m_EffectTexture = MaterialManager::GetTexture(effectMaterial->greyscaleTexture, renderer->GetBlackTextureDescriptor());

	auto effectData = reinterpret_cast<Data*>(m_Data.get());
	effectData->SourceTexture = m_SourceTexture.GetDescriptorIndex();
	effectData->EffectTexture = m_EffectTexture.GetDescriptorIndex();
}
