#if defined(FALLOUT4)

#include "Core/Material/Skyrim/LODLandscapeMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

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
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	data->TexOffsetX = runtime.terrainTexOffsetX;
	data->TexOffsetY = runtime.terrainTexOffsetY;
	data->TexFade = runtime.terrainTexFade;
}

void LODLandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	if (m_ParentDiffuseTexture.Update(runtime.parentDiffuseTexture, renderer->GetGrayTextureDescriptor()))
		data->ParentDiffuseTexture = m_ParentDiffuseTexture.texture.GetDescriptorIndex();
	if (m_ParentNormalTexture.Update(runtime.parentNormalTexture, renderer->GetNormalTextureDescriptor()))
		data->ParentNormalTexture = m_ParentNormalTexture.texture.GetDescriptorIndex();
	if (m_NoiseTexture.Update(runtime.terrainNoiseTexture, renderer->GetBlackTextureDescriptor()))
		data->NoiseTexture = m_NoiseTexture.texture.GetDescriptorIndex();
}

#endif
