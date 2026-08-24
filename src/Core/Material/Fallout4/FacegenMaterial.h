#pragma once

#include "Core/Material/Fallout4/LightingMaterial.h"
#include "interop/Material/Fallout4/FacegenMaterialData.hlsli"

struct FacegenMaterial : public LightingMaterial
{
	using Data = FacegenMaterialData;

	FacegenMaterial() = default;

	FacegenMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_FaceTexture;
};

