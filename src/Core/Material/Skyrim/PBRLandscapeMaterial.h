#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/PBRLandscapeMaterialData.hlsli"

struct PBRLandscapeMaterial : public LightingMaterial
{
	using Data = PBRLandscapeMaterialData;

	PBRLandscapeMaterial() = default;

	PBRLandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_BaseColorTextures[6];
	Texture m_NormalTextures[6];
	Texture m_RMAOSTextures[6];
	Texture m_OverlayTexture;
	Texture m_NoiseTexture;
};
