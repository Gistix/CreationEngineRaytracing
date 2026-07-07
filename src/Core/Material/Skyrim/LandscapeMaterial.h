#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/LandscapeMaterialData.hlsli"

struct LandscapeMaterial : public LightingMaterial
{
	using Data = LandscapeMaterialData;

	LandscapeMaterial() = default;

	LandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	// Layer 0 lives in the inherited LightingMaterial diffuse/normal; these are layers 1-5.
	Texture m_DiffuseTextures[5];
	Texture m_NormalTextures[5];
	Texture m_OverlayTexture;
	Texture m_NoiseTexture;
};
