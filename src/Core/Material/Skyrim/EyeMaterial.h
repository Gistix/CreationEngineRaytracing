#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/EyeMaterialData.hlsli"

struct EyeMaterial : public LightingMaterial
{
	using Data = EyeMaterialData;

	EyeMaterial() = default;

	EyeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_EnvironmentTexture;
	Texture m_EnvironmentMaskTexture;
};
