#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/FacegenMaterialData.hlsli"

struct FacegenMaterial : public LightingMaterial
{
	using Data = FacegenMaterialData;

	FacegenMaterial() = default;

	FacegenMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_TintTexture;
	MaterialTexture m_DetailTexture;
	MaterialTexture m_SubsurfaceTexture;
};
