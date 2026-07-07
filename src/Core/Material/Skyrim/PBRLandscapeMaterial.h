#pragma once

#include "Core/Material/MaterialBase.h"
#include "Core/Texture.h"
#include "Interop/Material/Skyrim/PBRLandscapeMaterialData.hlsli"

struct PBRLandscapeMaterial : public MaterialBase
{
	using Data = PBRLandscapeMaterialData;

	PBRLandscapeMaterial() = default;

	PBRLandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_DiffuseTexture;
	Texture m_NormalTexture;
	Texture m_RimSoftLightingTexture;
	Texture m_SpecularBackLightingTexture;
	Texture m_BaseColorTextures[6];
	Texture m_NormalTextures[6];
	Texture m_RMAOSTextures[6];
	Texture m_OverlayTexture;
	Texture m_NoiseTexture;
};
