#pragma once

#include "Core/Material/Fallout4/LightingMaterial.h"
#include "interop/Material/Fallout4/LandscapeMaterialData.hlsli"

struct LandscapeMaterial : public LightingMaterial
{
	using Data = LandscapeMaterialData;

	LandscapeMaterial() = default;

	LandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	virtual void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_DiffuseTextures[3];
	MaterialTexture m_NormalTextures[3];
	MaterialTexture m_SmoothSpecTextures[3];
	MaterialTexture m_OverlayTexture;
	MaterialTexture m_NoiseTexture;
};

