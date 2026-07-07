#include "Core/Material/Skyrim/WaterMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"

WaterMaterial::WaterMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<Data>();

	UpdateData(shaderMaterial);

	UpdateTextures(shaderMaterial);
}

void WaterMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	auto waterData = reinterpret_cast<Data*>(m_Data.get());

	waterData->Type = MaterialBase::Type::Water;
	waterData->Feature = static_cast<uint16_t>(shaderMaterial->GetFeature());

	waterData->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	waterData->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);

	auto waterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(shaderMaterial);

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
}

void WaterMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto renderer = Renderer::GetSingleton();
	auto& defaultNormal = renderer->GetNormalTextureDescriptor();

	auto waterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(shaderMaterial);
	m_NormalTexture0 = MaterialManager::GetTexture(waterMaterial->normalTexture1, defaultNormal);
	m_NormalTexture1 = MaterialManager::GetTexture(waterMaterial->normalTexture2, defaultNormal);
	m_NormalTexture2 = MaterialManager::GetTexture(waterMaterial->normalTexture3, defaultNormal);
	m_NormalTexture3 = MaterialManager::GetTexture(waterMaterial->normalTexture4, defaultNormal);

	auto waterData = reinterpret_cast<Data*>(m_Data.get());
	waterData->NormalsTexture0 = m_NormalTexture0.GetDescriptorIndex();
	waterData->NormalsTexture1 = m_NormalTexture1.GetDescriptorIndex();
	waterData->NormalsTexture2 = m_NormalTexture2.GetDescriptorIndex();
	waterData->FlowmapTexture = m_NormalTexture3.GetDescriptorIndex();
}
