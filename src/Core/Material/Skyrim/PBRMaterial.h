#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/PBRMaterialData.hlsli"

struct PBRMaterial : public LightingMaterial
{
	using Data = PBRMaterialData;

	PBRMaterial() = default;

	PBRMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	// Base color / normal reuse the inherited LightingMaterial diffuse/normal.
	Texture m_RMAOSTexture;
	Texture m_EmissiveTexture;
	Texture m_FeaturesTexture0;
	Texture m_FeaturesTexture1;
};
