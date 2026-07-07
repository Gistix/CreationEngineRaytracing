#pragma once

#include "Core/Material/MaterialBase.h"
#include "Interop/Material/Skyrim/LightingMaterialData.hlsli"
#include "Core/Texture.h"

struct LightingMaterial : public MaterialBase
{
	using Data = LightingMaterialData;

	LightingMaterial() = default;

	LightingMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_DiffuseTexture;
	Texture m_NormalTexture;
	Texture m_RimSoftLightingTexture;
	Texture m_SpecularBackLightingTexture;
};