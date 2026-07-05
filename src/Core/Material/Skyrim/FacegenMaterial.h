#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/FacegenMaterialData.hlsli"

struct FacegenMaterial : public LightingMaterial
{
	using Data = FacegenMaterialData;

	FacegenMaterial() = default;

	FacegenMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_TintTexture;
	Texture m_DetailTexture;
	Texture m_SubsurfaceTexture;
};
