#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/LODLandscapeMaterialData.hlsli"

struct LODLandscapeMaterial : public LightingMaterial
{
	using Data = LODLandscapeMaterialData;

	LODLandscapeMaterial() = default;

	LODLandscapeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_ParentDiffuseTexture;
	Texture m_ParentNormalTexture;
	Texture m_NoiseTexture;
};
