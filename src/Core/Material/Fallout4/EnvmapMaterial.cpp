#if defined(FALLOUT4)

#include "Core/Material/Fallout4/EnvmapMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

EnvmapMaterial::EnvmapMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<EnvmapMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void EnvmapMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
    auto* mat = static_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial);
	reinterpret_cast<Data*>(m_Data.get())->EnvironmentScale = mat->envMapScale;
}

void EnvmapMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial);
    
	if (m_EnvironmentTexture.Update(mat->envTexture.get(), renderer->GetBlackTextureDescriptor(), TextureType::CubeMap))
		data->EnvironmentTexture = m_EnvironmentTexture.texture.GetDescriptorIndex();
	if (m_EnvironmentMaskTexture.Update(mat->envMaskTexture.get(), renderer->GetWhiteTextureDescriptor()))
		data->EnvironmentMaskTexture = m_EnvironmentMaskTexture.texture.GetDescriptorIndex();
}

#endif
