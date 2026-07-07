#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/ParallaxOccMaterialData.hlsli"

struct ParallaxOccMaterial : public LightingMaterial
{
	using Data = ParallaxOccMaterialData;

	ParallaxOccMaterial() = default;

	ParallaxOccMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_HeightTexture;
};
