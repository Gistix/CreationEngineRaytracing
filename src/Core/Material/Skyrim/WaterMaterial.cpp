#include "Core/Material/Skyrim/WaterMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Scene.h"
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

	waterData->ShallowColor = Util::Math::Float3(waterMaterial->shallowWaterColor);

	waterData->NormalScroll1 = Util::Math::Float2(waterMaterial->normalScroll1);
	waterData->NormalScroll2 = Util::Math::Float2(waterMaterial->normalScroll2);
	waterData->NormalScroll3 = Util::Math::Float2(waterMaterial->normalScroll3);

	waterData->UVScale1 = waterMaterial->uvScaleA[0];
	waterData->UVScale2 = waterMaterial->uvScaleA[1];
	waterData->UVScale3 = waterMaterial->uvScaleA[2];

	waterData->Amplitude1 = waterMaterial->amplitudeA[0];
	waterData->Amplitude2 = waterMaterial->amplitudeA[1];
	waterData->Amplitude3 = waterMaterial->amplitudeA[2];
	waterData->Amplitude4 = waterMaterial->displacementDampener;
}

void WaterMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto renderer = Renderer::GetSingleton();
	auto& defaultNormal = renderer->GetNormalTextureDescriptor();

	auto waterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(shaderMaterial);
	auto waterData = reinterpret_cast<Data*>(m_Data.get());

	if (m_NormalTexture1.Update(waterMaterial->normalTexture1, defaultNormal))
		waterData->NormalsTexture1 = m_NormalTexture1.texture.GetDescriptorIndex();

	if (m_NormalTexture2.Update(waterMaterial->normalTexture2, defaultNormal))
		waterData->NormalsTexture2 = m_NormalTexture2.texture.GetDescriptorIndex();

	if (m_NormalTexture3.Update(waterMaterial->normalTexture3, defaultNormal))
		waterData->NormalsTexture3 = m_NormalTexture3.texture.GetDescriptorIndex();

	if (m_NormalTexture4.Update(waterMaterial->normalTexture4, defaultNormal))
		waterData->NormalsTexture4 = m_NormalTexture4.texture.GetDescriptorIndex();
}
