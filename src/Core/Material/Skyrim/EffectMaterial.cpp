#include "Core/Material/Skyrim/EffectMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"

EffectMaterial::EffectMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<Data>();

	Initialize(m_Data.get(), shaderMaterial);
}

void EffectMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	auto effectData = reinterpret_cast<Data*>(data);

	effectData->Type = MaterialBase::Type::Effect;
	effectData->Feature = static_cast<uint16_t>(shaderMaterial->GetFeature());

	effectData->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	effectData->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);

	auto effectMaterial = skyrim_cast<RE::BSEffectShaderMaterial*>(shaderMaterial);
	if (!effectMaterial) {
		logger::error("EffectMaterial::Initialize - Shader material is not BSEffectShaderMaterial");
		return;
	}

	effectData->BaseColor = float4(effectMaterial->baseColor.red, effectMaterial->baseColor.green,
	                               effectMaterial->baseColor.blue, effectMaterial->baseColor.alpha);
	effectData->BaseColorScale = effectMaterial->baseColorScale;

	UpdateTextures(shaderMaterial);

	effectData->SourceTexture = m_SourceTexture.GetDescriptorIndex();
	effectData->EffectTexture = m_EffectTexture.GetDescriptorIndex();
}

void EffectMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto effectMaterial = skyrim_cast<RE::BSEffectShaderMaterial*>(shaderMaterial);
	if (!effectMaterial) {
		logger::error("EffectMaterial::UpdateTextures - Shader material is not BSEffectShaderMaterial");
		return;
	}

	auto renderer = Renderer::GetSingleton();

	m_SourceTexture = MaterialManager::GetTexture(effectMaterial->sourceTexture, renderer->GetBlackTextureDescriptor());
	m_EffectTexture = MaterialManager::GetTexture(effectMaterial->greyscaleTexture, renderer->GetBlackTextureDescriptor());
}
