#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/LODLandscapeMaterialData.hlsli"

struct LODLandscapeMaterial : public LightingMaterial
{
	using Data = LODLandscapeMaterialData;

	LODLandscapeMaterial() = default;

	LODLandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_ParentDiffuseTexture;
	MaterialTexture m_ParentNormalTexture;
	MaterialTexture m_NoiseTexture;
};
