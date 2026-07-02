#pragma once

#include "Core/Material/MaterialBase.h"
#include "Core/Texture.h"
#include "Interop/Material/Skyrim/WaterMaterialData.hlsli"

struct WaterMaterial : public MaterialBase
{
	using Data = WaterMaterialData;

	WaterMaterial() = default;

	WaterMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }

	Texture m_NormalTexture0;
	Texture m_NormalTexture1;
	Texture m_NormalTexture2;
	Texture m_NormalTexture3;
};
