#if defined(FALLOUT4)

#include "Core/Material/Fallout4/LODLandscapeMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

LODLandscapeMaterial::LODLandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<LODLandscapeMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void LODLandscapeMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
}

void LODLandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialLODLandscape*>(shaderMaterial);

	if (m_ParentDiffuseTexture.Update(mat->parentDiffuseTexture.get(), renderer->GetGrayTextureDescriptor()))
		data->ParentDiffuseTexture = m_ParentDiffuseTexture.texture.GetDescriptorIndex();
	if (m_ParentNormalTexture.Update(mat->parentNormalTexture.get(), renderer->GetNormalTextureDescriptor()))
		data->ParentNormalTexture = m_ParentNormalTexture.texture.GetDescriptorIndex();
	if (m_NoiseTexture.Update(mat->landscapeNoiseTexture.get(), renderer->GetBlackTextureDescriptor()))
		data->NoiseTexture = m_NoiseTexture.texture.GetDescriptorIndex();
}

#endif
