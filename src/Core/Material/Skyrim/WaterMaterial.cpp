#include "Core/Material/Skyrim/WaterMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"

WaterMaterial::WaterMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<Data>();

	Initialize(m_Data.get(), shaderMaterial);
}

void WaterMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	auto waterData = reinterpret_cast<Data*>(data);

	waterData->Type = MaterialBase::Type::Water;
	waterData->Feature = static_cast<uint16_t>(shaderMaterial->GetFeature());

	waterData->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	waterData->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);

	auto waterMaterial = skyrim_cast<RE::BSWaterShaderMaterial*>(shaderMaterial);
	if (!waterMaterial) {
		logger::error("WaterMaterial::Initialize - Shader material is not BSWaterShaderMaterial");
		return;
	}

	waterData->ShallowColor = half4(
		waterMaterial->shallowWaterColor.red,
		waterMaterial->shallowWaterColor.green,
		waterMaterial->shallowWaterColor.blue,
		1.0f);

	waterData->Amplitude0 = waterMaterial->amplitudeA[0];
	waterData->Amplitude1 = waterMaterial->amplitudeA[1];
	waterData->Amplitude2 = waterMaterial->amplitudeA[2];

	waterData->NormalScrolls = half4(
		waterMaterial->normalScroll1.x, waterMaterial->normalScroll1.y,
		waterMaterial->normalScroll2.x, waterMaterial->normalScroll2.y);
	waterData->NormalScroll3AndScale = half4(
		waterMaterial->normalScroll3.x, waterMaterial->normalScroll3.y,
		waterMaterial->uvScaleA[0], waterMaterial->uvScaleA[1]);
	waterData->UVScaleAndObjectUV.x = waterMaterial->uvScaleA[2];

	UpdateTextures(shaderMaterial);

	waterData->NormalsTexture0 = m_NormalTexture0.GetDescriptorIndex();
	waterData->NormalsTexture1 = m_NormalTexture1.GetDescriptorIndex();
	waterData->NormalsTexture2 = m_NormalTexture2.GetDescriptorIndex();
	waterData->FlowmapTexture = m_NormalTexture3.GetDescriptorIndex();
}

void WaterMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto waterMaterial = skyrim_cast<RE::BSWaterShaderMaterial*>(shaderMaterial);
	if (!waterMaterial) {
		logger::error("WaterMaterial::UpdateTextures - Shader material is not BSWaterShaderMaterial");
		return;
	}

	auto renderer = Renderer::GetSingleton();

	auto& defaultNormal = renderer->GetNormalTextureDescriptor();
	m_NormalTexture0 = MaterialManager::GetTexture(waterMaterial->normalTexture1, defaultNormal);
	m_NormalTexture1 = MaterialManager::GetTexture(waterMaterial->normalTexture2, defaultNormal);
	m_NormalTexture2 = MaterialManager::GetTexture(waterMaterial->normalTexture3, defaultNormal);
	m_NormalTexture3 = MaterialManager::GetTexture(waterMaterial->normalTexture4, defaultNormal);
}
