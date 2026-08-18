#if defined(FALLOUT4)

#include "Core/Material/Fallout4/WaterMaterial.h"
#include "Renderer.h"
#include "RE/B/BSWaterShaderMaterial.h"

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
	auto* water = static_cast<RE::BSWaterShaderMaterial*>(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	data->Type = MaterialBase::Type::Water;
	data->ShallowColor.x = water->shallowColor.r; data->ShallowColor.y = water->shallowColor.g; data->ShallowColor.z = water->shallowColor.b;
	
    // NiColorA is 4 floats.
	data->NormalScroll1.x = water->normalsScroll1.r; data->NormalScroll1.y = water->normalsScroll1.g;
	data->NormalScroll2.x = water->normalsScroll1.b; data->NormalScroll2.y = water->normalsScroll1.a;
	data->NormalScroll3.x = water->normalsScroll2.r; data->NormalScroll3.y = water->normalsScroll2.g;
	
	data->UVScale1 = water->normalsScale.r;
	data->UVScale2 = water->normalsScale.b;
	data->UVScale3 = water->normalsAmplitude.r; // Assuming amplitude/scale packed

	data->Amplitude1 = water->normalsAmplitude.r;
	data->Amplitude2 = water->normalsAmplitude.g;
	data->Amplitude3 = water->normalsAmplitude.b;
	data->Amplitude4 = water->normalsAmplitude.a;
}

void WaterMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto* water = static_cast<RE::BSWaterShaderMaterial*>(shaderMaterial);
	const auto renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    
	MaterialTexture* textures[] = { &m_NormalTexture1, &m_NormalTexture2, &m_NormalTexture3, &m_NormalTexture4 };
    RE::NiTexture* runtimeTextures[] = { water->normalMap01.get(), water->normalMap02.get(), water->normalMap03.get(), nullptr };
    
	for (uint32_t i = 0; i < 4; ++i) {
		if (textures[i]->Update(runtimeTextures[i], renderer->GetNormalTextureDescriptor()))
			(&data->NormalsTexture1)[i] = textures[i]->texture.GetDescriptorIndex();
	}
}

#endif


