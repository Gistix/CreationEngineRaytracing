#if defined(FALLOUT4)

#include "Core/Material/Skyrim/EyeMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

EyeMaterial::EyeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<EyeMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void EyeMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
	reinterpret_cast<Data*>(m_Data.get())->EnvironmentScale = Util::Adapter::GetMaterialRuntimeData(shaderMaterial).environmentScale;
}

void EyeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	if (m_EnvironmentTexture.Update(runtime.environmentTexture, renderer->GetBlackTextureDescriptor(), TextureType::CubeMap))
		data->EnvironmentTexture = m_EnvironmentTexture.texture.GetDescriptorIndex();
	if (m_EnvironmentMaskTexture.Update(runtime.environmentMaskTexture, renderer->GetWhiteTextureDescriptor()))
		data->EnvironmentMaskTexture = m_EnvironmentMaskTexture.texture.GetDescriptorIndex();
}

#endif
