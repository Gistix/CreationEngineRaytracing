#if defined(FALLOUT4)

#include "Core/Material/Fallout4/EyeMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

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
    auto* mat = static_cast<RE::BSLightingShaderMaterialEye*>(shaderMaterial);
	reinterpret_cast<Data*>(m_Data.get())->EnvironmentScale = mat->envMapScale;
}

void EyeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialEye*>(shaderMaterial);

	if (m_EnvironmentTexture.Update(mat->envTexture.get(), renderer->GetBlackTextureDescriptor(), TextureType::CubeMap))
		data->EnvironmentTexture = m_EnvironmentTexture.texture.GetDescriptorIndex();
	if (m_EnvironmentMaskTexture.Update(mat->envMaskTexture.get(), renderer->GetWhiteTextureDescriptor()))
		data->EnvironmentMaskTexture = m_EnvironmentMaskTexture.texture.GetDescriptorIndex();
}

#endif
