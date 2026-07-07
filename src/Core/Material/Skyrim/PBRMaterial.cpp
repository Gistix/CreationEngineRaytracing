#include "Core/Material/Skyrim/PBRMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"
#include "Utils/Material.h"
#include "Types/CommunityShaders/BSLightingShaderMaterialPBR.h"

PBRMaterial::PBRMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<Data>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void PBRMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	auto pbrData = reinterpret_cast<Data*>(m_Data.get());

	pbrData->Type = MaterialBase::Type::TruePBR;
	pbrData->Feature = static_cast<uint16_t>(shaderMaterial->GetFeature());

	pbrData->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	pbrData->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);

	auto pbrMaterial = reinterpret_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

	pbrData->SpecularColor = Util::Math::Float3(pbrMaterial->specularColor);
	pbrData->SpecularLevel = pbrMaterial->GetSpecularLevel();
	pbrData->RoughnessScale = pbrMaterial->GetRoughnessScale();
	pbrData->MaterialAlpha = pbrMaterial->materialAlpha;

	pbrData->PBRFlags = static_cast<uint16_t>(Util::Material::Skyrim::GetPBRShaderFlags(pbrMaterial).underlying());

	const auto& pbrFlags = pbrMaterial->pbrFlags;

	if (pbrFlags.any(PBRFlags::Subsurface)) {
		const auto& subsurfaceColor = pbrMaterial->GetSubsurfaceColor();
		pbrData->FeatureColor = float4(subsurfaceColor.red, subsurfaceColor.green, subsurfaceColor.blue, 1.0f);
		pbrData->FeatureScalar = pbrMaterial->GetSubsurfaceOpacity();
	}
	else if (pbrFlags.any(PBRFlags::TwoLayer)) {
		const auto& coatColor = pbrMaterial->GetSubsurfaceColor();
		pbrData->FeatureColor = float4(coatColor.red, coatColor.green, coatColor.blue, pbrMaterial->GetSubsurfaceOpacity());
		pbrData->FeatureScalar = pbrMaterial->coatRoughness;
	}
	else if (pbrFlags.any(PBRFlags::Fuzz)) {
		pbrData->FeatureColor = float4(pbrMaterial->fuzzColor.red, pbrMaterial->fuzzColor.green, pbrMaterial->fuzzColor.blue, pbrMaterial->fuzzWeight);
	}

	pbrData->DisplacementScale = pbrMaterial->GetDisplacementScale();

	if (pbrMaterial->glintParameters.enabled) {
		const auto& glint = pbrMaterial->GetGlintParameters();
		pbrData->GlintParameters = float4(glint.screenSpaceScale, glint.logMicrofacetDensity, glint.microfacetRoughness, glint.densityRandomization);
	}
}

void PBRMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	auto pbrMaterial = reinterpret_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

	auto renderer = Renderer::GetSingleton();

	auto pbrData = reinterpret_cast<Data*>(m_Data.get());

	if (m_DiffuseTexture.Update(pbrMaterial->diffuseTexture, renderer->GetGrayTextureDescriptor()))
		pbrData->DiffuseTexture = m_DiffuseTexture.texture.GetDescriptorIndex();

	if (m_NormalTexture.Update(pbrMaterial->normalTexture, renderer->GetNormalTextureDescriptor()))
		pbrData->NormalTexture = m_NormalTexture.texture.GetDescriptorIndex();

	if (m_RimSoftLightingTexture.Update(pbrMaterial->rimSoftLightingTexture, renderer->GetBlackTextureDescriptor()))
		pbrData->RimSoftLightingTexture = m_RimSoftLightingTexture.texture.GetDescriptorIndex();

	if (m_SpecularBackLightingTexture.Update(pbrMaterial->specularBackLightingTexture, renderer->GetBlackTextureDescriptor()))
		pbrData->SpecularBackLightingTexture = m_SpecularBackLightingTexture.texture.GetDescriptorIndex();

	if (m_RMAOSTexture.Update(pbrMaterial->rmaosTexture, renderer->GetRMAOSTextureDescriptor()))
		pbrData->RMAOSTexture = m_RMAOSTexture.texture.GetDescriptorIndex();

	if (m_EmissiveTexture.Update(pbrMaterial->emissiveTexture, renderer->GetBlackTextureDescriptor()))
		pbrData->EmissiveTexture = m_EmissiveTexture.texture.GetDescriptorIndex();

	if (m_DisplacementTexture.Update(pbrMaterial->displacementTexture, renderer->GetBlackTextureDescriptor()))
		pbrData->DisplacementTexture = m_DisplacementTexture.texture.GetDescriptorIndex();

	if (m_FeaturesTexture0.Update(pbrMaterial->featuresTexture0, renderer->GetWhiteTextureDescriptor()))
		pbrData->FeaturesTexture0 = m_FeaturesTexture0.texture.GetDescriptorIndex();

	if (m_FeaturesTexture1.Update(pbrMaterial->featuresTexture1, renderer->GetWhiteTextureDescriptor()))
		pbrData->FeaturesTexture1 = m_FeaturesTexture1.texture.GetDescriptorIndex();
}
