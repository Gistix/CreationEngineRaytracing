#if defined(FALLOUT4)

#include "Core/Material/Skyrim/WaterMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

WaterMaterial::WaterMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<WaterMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void WaterMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	MaterialBase::UpdateData(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	data->Type = MaterialBase::Type::Water;
	data->ShallowColor = runtime.shallowWaterColor;
	data->NormalScroll1 = runtime.normalScroll[0];
	data->NormalScroll2 = runtime.normalScroll[1];
	data->NormalScroll3 = runtime.normalScroll[2];
	data->UVScale1 = runtime.uvScale[0];
	data->UVScale2 = runtime.uvScale[1];
	data->UVScale3 = runtime.uvScale[2];
	data->Amplitude1 = runtime.amplitude[0];
	data->Amplitude2 = runtime.amplitude[1];
	data->Amplitude3 = runtime.amplitude[2];
	data->Amplitude4 = runtime.displacementDampener;
}

void WaterMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	const auto renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	MaterialTexture* textures[] = { &m_NormalTexture1, &m_NormalTexture2, &m_NormalTexture3, &m_NormalTexture4 };
	for (uint32_t i = 0; i < 4; ++i) {
		if (textures[i]->Update(runtime.waterNormalTextures[i], renderer->GetNormalTextureDescriptor()))
			(&data->NormalsTexture1)[i] = textures[i]->texture.GetDescriptorIndex();
	}
}

#endif
