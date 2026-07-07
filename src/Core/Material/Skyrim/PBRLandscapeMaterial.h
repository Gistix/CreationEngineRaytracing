#pragma once

#include "Core/Material/MaterialBase.h"
#include "Core/MaterialTexture.h"
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

	MaterialTexture m_DiffuseTexture;
	MaterialTexture m_NormalTexture;
	MaterialTexture m_RimSoftLightingTexture;
	MaterialTexture m_SpecularBackLightingTexture;
	MaterialTexture m_BaseColorTextures[6];
	MaterialTexture m_NormalTextures[6];
	MaterialTexture m_RMAOSTextures[6];
	MaterialTexture m_OverlayTexture;
	MaterialTexture m_NoiseTexture;
};
