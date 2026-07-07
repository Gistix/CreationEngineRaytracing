#pragma once

#include "Core/Material/MaterialBase.h"
#include "Core/MaterialTexture.h"
#include "Interop/Material/Skyrim/WaterMaterialData.hlsli"

struct WaterMaterial : public MaterialBase
{
	using Data = WaterMaterialData;

	WaterMaterial() = default;

	WaterMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_NormalTexture0;
	MaterialTexture m_NormalTexture1;
	MaterialTexture m_NormalTexture2;
	MaterialTexture m_NormalTexture3;
};
