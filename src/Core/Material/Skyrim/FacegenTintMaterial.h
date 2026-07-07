#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/FacegenTintMaterialData.hlsli"

struct FacegenTintMaterial : public LightingMaterial
{
	using Data = FacegenTintMaterialData;

	FacegenTintMaterial() = default;

	FacegenTintMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }
};
