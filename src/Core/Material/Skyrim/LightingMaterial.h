#pragma once

#include "Core/Material/MaterialBase.h"
#include "Interop/Material/Skyrim/LightingMaterialData.hlsli"

struct LightingMaterial : public MaterialBase
{
	using Data = LightingMaterialData;

	LightingMaterial() = default;

	LightingMaterial(RE::BSShaderMaterial* shaderMaterial);

	static void Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }
};