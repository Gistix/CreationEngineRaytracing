#pragma once

#include "Core/Material/Skyrim/LightingMaterial.h"
#include "Interop/Material/Skyrim/MultiLayerParallaxMaterialData.hlsli"

struct MultiLayerParallaxMaterial : public LightingMaterial
{
	using Data = MultiLayerParallaxMaterialData;

	MultiLayerParallaxMaterial() = default;

	MultiLayerParallaxMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_LayerTexture;
	MaterialTexture m_EnvironmentTexture;
	MaterialTexture m_EnvironmentMaskTexture;
};
