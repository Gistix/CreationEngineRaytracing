#pragma once

#include "Interop/Material/MaterialBaseData.hlsli"

struct MaterialBase
{
	using Data = MaterialBaseData;

	MaterialBase() = default;

	MaterialBase(RE::BSShaderMaterial* shaderMaterial);

	static void Initialize(Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual Data* GetData() { return m_Data.get(); }

	virtual size_t GetDataSize() { return sizeof(Data); }
protected:
	eastl::unique_ptr<Data> m_Data;
};