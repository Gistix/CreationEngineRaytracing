#include "Core/Material/Skyrim/PBRLandscapeMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"
#include "Utils/Material.h"
#include "Types/CommunityShaders/BSLightingShaderMaterialPBRLandscape.h"

PBRLandscapeMaterial::PBRLandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<Data>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void PBRLandscapeMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	auto landData = reinterpret_cast<Data*>(m_Data.get());

	landData->Type = MaterialBase::Type::TruePBR;
	landData->Feature = static_cast<uint16_t>(RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend);

	landData->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	landData->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);

	auto landMaterial = reinterpret_cast<BSLightingShaderMaterialPBRLandscape*>(shaderMaterial);

	landData->SpecularColor = Util::Math::Float3(landMaterial->specularColor);
	landData->SpecularLevel = landMaterial->specularPower;
	landData->RoughnessScale = landMaterial->specularColorScale;
	landData->MaterialAlpha = landMaterial->materialAlpha;

	landData->PBRFlags = static_cast<uint16_t>(Util::Material::Skyrim::GetPBRShaderFlags(landMaterial).underlying());

	landData->RoughnessScale0 = landMaterial->roughnessScales[0];
	landData->RoughnessScale1 = landMaterial->roughnessScales[1];
	landData->RoughnessScale2 = landMaterial->roughnessScales[2];
	landData->RoughnessScale3 = landMaterial->roughnessScales[3];
	landData->RoughnessScale4 = landMaterial->roughnessScales[4];
	landData->RoughnessScale5 = landMaterial->roughnessScales[5];

	landData->DisplacementScale0 = landMaterial->displacementScales[0];
	landData->DisplacementScale1 = landMaterial->displacementScales[1];
	landData->DisplacementScale2 = landMaterial->displacementScales[2];
	landData->DisplacementScale3 = landMaterial->displacementScales[3];
	landData->DisplacementScale4 = landMaterial->displacementScales[4];
	landData->DisplacementScale5 = landMaterial->displacementScales[5];

	landData->SpecularLevel0 = landMaterial->specularLevels[0];
	landData->SpecularLevel1 = landMaterial->specularLevels[1];
	landData->SpecularLevel2 = landMaterial->specularLevels[2];
	landData->SpecularLevel3 = landMaterial->specularLevels[3];
	landData->SpecularLevel4 = landMaterial->specularLevels[4];
	landData->SpecularLevel5 = landMaterial->specularLevels[5];
}

void PBRLandscapeMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto landMaterial = reinterpret_cast<BSLightingShaderMaterialPBRLandscape*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	auto landData = reinterpret_cast<Data*>(m_Data.get());

	if (m_DiffuseTexture.Update(landMaterial->diffuseTexture, renderer->GetGrayTextureDescriptor()))
		landData->DiffuseTexture = m_DiffuseTexture.texture.GetDescriptorIndex();

	if (m_NormalTexture.Update(landMaterial->normalTexture, renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture = m_NormalTexture.texture.GetDescriptorIndex();

	if (m_RimSoftLightingTexture.Update(landMaterial->rimSoftLightingTexture, renderer->GetBlackTextureDescriptor()))
		landData->RimSoftLightingTexture = m_RimSoftLightingTexture.texture.GetDescriptorIndex();

	if (m_SpecularBackLightingTexture.Update(landMaterial->specularBackLightingTexture, renderer->GetBlackTextureDescriptor()))
		landData->SpecularBackLightingTexture = m_SpecularBackLightingTexture.texture.GetDescriptorIndex();

	if (m_BaseColorTextures[0].Update(landMaterial->landscapeBaseColorTextures[0], renderer->GetGrayTextureDescriptor()))
		landData->BaseColorTexture0 = m_BaseColorTextures[0].texture.GetDescriptorIndex();
	if (m_BaseColorTextures[1].Update(landMaterial->landscapeBaseColorTextures[1], renderer->GetGrayTextureDescriptor()))
		landData->BaseColorTexture1 = m_BaseColorTextures[1].texture.GetDescriptorIndex();
	if (m_BaseColorTextures[2].Update(landMaterial->landscapeBaseColorTextures[2], renderer->GetGrayTextureDescriptor()))
		landData->BaseColorTexture2 = m_BaseColorTextures[2].texture.GetDescriptorIndex();
	if (m_BaseColorTextures[3].Update(landMaterial->landscapeBaseColorTextures[3], renderer->GetGrayTextureDescriptor()))
		landData->BaseColorTexture3 = m_BaseColorTextures[3].texture.GetDescriptorIndex();
	if (m_BaseColorTextures[4].Update(landMaterial->landscapeBaseColorTextures[4], renderer->GetGrayTextureDescriptor()))
		landData->BaseColorTexture4 = m_BaseColorTextures[4].texture.GetDescriptorIndex();
	if (m_BaseColorTextures[5].Update(landMaterial->landscapeBaseColorTextures[5], renderer->GetGrayTextureDescriptor()))
		landData->BaseColorTexture5 = m_BaseColorTextures[5].texture.GetDescriptorIndex();

	if (m_NormalTextures[0].Update(landMaterial->landscapeNormalTextures[0], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture0 = m_NormalTextures[0].texture.GetDescriptorIndex();
	if (m_NormalTextures[1].Update(landMaterial->landscapeNormalTextures[1], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture1 = m_NormalTextures[1].texture.GetDescriptorIndex();
	if (m_NormalTextures[2].Update(landMaterial->landscapeNormalTextures[2], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture2 = m_NormalTextures[2].texture.GetDescriptorIndex();
	if (m_NormalTextures[3].Update(landMaterial->landscapeNormalTextures[3], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture3 = m_NormalTextures[3].texture.GetDescriptorIndex();
	if (m_NormalTextures[4].Update(landMaterial->landscapeNormalTextures[4], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture4 = m_NormalTextures[4].texture.GetDescriptorIndex();
	if (m_NormalTextures[5].Update(landMaterial->landscapeNormalTextures[5], renderer->GetNormalTextureDescriptor()))
		landData->NormalTexture5 = m_NormalTextures[5].texture.GetDescriptorIndex();

	if (m_DisplacementTextures[0].Update(landMaterial->landscapeDisplacementTextures[0], renderer->GetBlackTextureDescriptor()))
		landData->DisplacementTexture0 = m_DisplacementTextures[0].texture.GetDescriptorIndex();
	if (m_DisplacementTextures[1].Update(landMaterial->landscapeDisplacementTextures[1], renderer->GetBlackTextureDescriptor()))
		landData->DisplacementTexture1 = m_DisplacementTextures[1].texture.GetDescriptorIndex();
	if (m_DisplacementTextures[2].Update(landMaterial->landscapeDisplacementTextures[2], renderer->GetBlackTextureDescriptor()))
		landData->DisplacementTexture2 = m_DisplacementTextures[2].texture.GetDescriptorIndex();
	if (m_DisplacementTextures[3].Update(landMaterial->landscapeDisplacementTextures[3], renderer->GetBlackTextureDescriptor()))
		landData->DisplacementTexture3 = m_DisplacementTextures[3].texture.GetDescriptorIndex();
	if (m_DisplacementTextures[4].Update(landMaterial->landscapeDisplacementTextures[4], renderer->GetBlackTextureDescriptor()))
		landData->DisplacementTexture4 = m_DisplacementTextures[4].texture.GetDescriptorIndex();
	if (m_DisplacementTextures[5].Update(landMaterial->landscapeDisplacementTextures[5], renderer->GetBlackTextureDescriptor()))
		landData->DisplacementTexture5 = m_DisplacementTextures[5].texture.GetDescriptorIndex();

	if (m_RMAOSTextures[0].Update(landMaterial->landscapeRMAOSTextures[0], renderer->GetRMAOSTextureDescriptor()))
		landData->RMAOSTexture0 = m_RMAOSTextures[0].texture.GetDescriptorIndex();
	if (m_RMAOSTextures[1].Update(landMaterial->landscapeRMAOSTextures[1], renderer->GetRMAOSTextureDescriptor()))
		landData->RMAOSTexture1 = m_RMAOSTextures[1].texture.GetDescriptorIndex();
	if (m_RMAOSTextures[2].Update(landMaterial->landscapeRMAOSTextures[2], renderer->GetRMAOSTextureDescriptor()))
		landData->RMAOSTexture2 = m_RMAOSTextures[2].texture.GetDescriptorIndex();
	if (m_RMAOSTextures[3].Update(landMaterial->landscapeRMAOSTextures[3], renderer->GetRMAOSTextureDescriptor()))
		landData->RMAOSTexture3 = m_RMAOSTextures[3].texture.GetDescriptorIndex();
	if (m_RMAOSTextures[4].Update(landMaterial->landscapeRMAOSTextures[4], renderer->GetRMAOSTextureDescriptor()))
		landData->RMAOSTexture4 = m_RMAOSTextures[4].texture.GetDescriptorIndex();
	if (m_RMAOSTextures[5].Update(landMaterial->landscapeRMAOSTextures[5], renderer->GetRMAOSTextureDescriptor()))
		landData->RMAOSTexture5 = m_RMAOSTextures[5].texture.GetDescriptorIndex();

	if (m_OverlayTexture.Update(landMaterial->terrainOverlayTexture, renderer->GetBlackTextureDescriptor()))
		landData->OverlayTexture = m_OverlayTexture.texture.GetDescriptorIndex();

	if (m_NoiseTexture.Update(landMaterial->terrainNoiseTexture, renderer->GetBlackTextureDescriptor()))
		landData->NoiseTexture = m_NoiseTexture.texture.GetDescriptorIndex();
}
