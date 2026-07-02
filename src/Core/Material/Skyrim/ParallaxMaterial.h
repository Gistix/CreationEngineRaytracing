#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/ParallaxMaterialData.hlsli"

struct ParallaxMaterial : public LightingMaterial
{
	using Data = ParallaxMaterialData;

	ParallaxMaterial() = default;

	ParallaxMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_HeightTexture;
};
