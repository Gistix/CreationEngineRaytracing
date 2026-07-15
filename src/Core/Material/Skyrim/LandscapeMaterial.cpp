#include "Core/Material/Skyrim/LandscapeMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"

LandscapeMaterial::LandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<LandscapeMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void LandscapeMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
}

void LandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);

	auto landMaterial = reinterpret_cast<RE::BSLightingShaderMaterialLandscape*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	auto landData = reinterpret_cast<Data*>(m_Data.get());

	if (m_DiffuseTextures[0].Update(landMaterial->landscapeDiffuseTexture[0], renderer->GetGrayTextureDescriptor()))
		landData->DiffuseTexture1 = m_DiffuseTextures[0].texture.GetDescriptorIndex();
	if (m_DiffuseTextures[1].Update(landMaterial->landscapeDiffuseTexture[1], renderer->GetGrayTextureDescriptor()))
		landData->DiffuseTexture2 = m_DiffuseTextures[1].texture.GetDescriptorIndex();
	if (m_DiffuseTextures[2].Update(landMaterial->landscapeDiffuseTexture[2], renderer->GetGrayTextureDescriptor()))
		landData->DiffuseTexture3 = m_DiffuseTextures[2].texture.GetDescriptorIndex();
	if (m_DiffuseTextures[3].Update(landMaterial->landscapeDiffuseTexture[3], renderer->GetGrayTextureDescriptor()))
		landData->DiffuseTexture4 = m_DiffuseTextures[3].texture.GetDescriptorIndex();
	if (m_DiffuseTextures[4].Update(landMaterial->landscapeDiffuseTexture[4], renderer->GetGrayTextureDescriptor()))
		landData->DiffuseTexture5 = m_DiffuseTextures[4].texture.GetDescriptorIndex();

	if (m_NormalTextures[0].Update(landMaterial->landscapeNormalTexture[0], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture1 = m_NormalTextures[0].texture.GetDescriptorIndex();
	if (m_NormalTextures[1].Update(landMaterial->landscapeNormalTexture[1], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture2 = m_NormalTextures[1].texture.GetDescriptorIndex();
	if (m_NormalTextures[2].Update(landMaterial->landscapeNormalTexture[2], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture3 = m_NormalTextures[2].texture.GetDescriptorIndex();
	if (m_NormalTextures[3].Update(landMaterial->landscapeNormalTexture[3], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture4 = m_NormalTextures[3].texture.GetDescriptorIndex();
	if (m_NormalTextures[4].Update(landMaterial->landscapeNormalTexture[4], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture5 = m_NormalTextures[4].texture.GetDescriptorIndex();

	if (m_OverlayTexture.Update(landMaterial->terrainOverlayTexture, renderer->GetBlackTextureDescriptor()))
		landData->OverlayTexture = m_OverlayTexture.texture.GetDescriptorIndex();

	if (m_NoiseTexture.Update(landMaterial->terrainNoiseTexture, renderer->GetBlackTextureDescriptor()))
		landData->NoiseTexture = m_NoiseTexture.texture.GetDescriptorIndex();
}
