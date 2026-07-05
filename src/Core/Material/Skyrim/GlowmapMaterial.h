#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/GlowmapMaterialData.hlsli"

struct GlowmapMaterial : public LightingMaterial
{
	using Data = GlowmapMaterialData;

	GlowmapMaterial() = default;

	GlowmapMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_GlowTexture;
};
