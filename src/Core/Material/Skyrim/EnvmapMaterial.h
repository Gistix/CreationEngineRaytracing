#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/EnvmapMaterialData.hlsli"

struct EnvmapMaterial : public LightingMaterial
{
	using Data = EnvmapMaterialData;

	EnvmapMaterial() = default;

	EnvmapMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_EnvironmentTexture;
	Texture m_EnvironmentMaskTexture;
};
