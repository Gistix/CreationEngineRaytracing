#pragma once

#include "Core/Material/MaterialBase.h"
#include "Interop/Material/Skyrim/LightingMaterialData.hlsli"
#include "Core/MaterialTexture.h"

struct LightingMaterial : public MaterialBase
{
	using Data = LightingMaterialData;

	LightingMaterial() = default;

	LightingMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_DiffuseTexture;
	MaterialTexture m_NormalTexture;
	MaterialTexture m_RimSoftLightingTexture;
	MaterialTexture m_SpecularBackLightingTexture;
};