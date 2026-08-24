#pragma once

#include "Core/Material/Fallout4/LightingMaterial.h"
#include "interop/Material/Fallout4/EnvmapMaterialData.hlsli"

struct EnvmapMaterial : public LightingMaterial
{
	using Data = EnvmapMaterialData;

	EnvmapMaterial() = default;

	EnvmapMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_EnvironmentTexture;
	MaterialTexture m_EnvironmentMaskTexture;
};

