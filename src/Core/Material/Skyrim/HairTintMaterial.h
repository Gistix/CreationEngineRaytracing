#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/HairTintMaterialData.hlsli"

struct HairTintMaterial : public LightingMaterial
{
	using Data = HairTintMaterialData;

	HairTintMaterial() = default;

	HairTintMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual size_t GetDataSize() override { return sizeof(Data); }
};
