#pragma once

#include "Core/Material/MaterialBase.h"
#include "Core/Texture.h"
#include "Interop/Material/Skyrim/EffectMaterialData.hlsli"

struct EffectMaterial : public MaterialBase
{
	using Data = EffectMaterialData;

	EffectMaterial() = default;

	EffectMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_SourceTexture;
	Texture m_EffectTexture;
};
