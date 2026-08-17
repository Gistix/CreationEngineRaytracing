#pragma once

#include "Core/Material/Fallout4/LightingMaterial.h"
#include "interop/Material/Fallout4/HairTintMaterialData.hlsli"

struct HairTintMaterial : public LightingMaterial
{
	using Data = HairTintMaterialData;

	HairTintMaterial() = default;

	HairTintMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }
};

