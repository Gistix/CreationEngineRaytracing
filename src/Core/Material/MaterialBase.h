#pragma once

#include "Interop/Material/MaterialBaseData.hlsli"

struct MaterialBase
{
	using Data = MaterialBaseData;

	MaterialBase() = default;

	MaterialBase(RE::BSShaderMaterial* shaderMaterial);

	void Initialize(Data* data, RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial);

	virtual Data* GetData() { return m_Data.get(); }

	virtual size_t GetDataSize() { return sizeof(Data); }
protected:
	// Material buffer offset
	uint64_t m_Offset;

	// Material data sent to the GPU
	eastl::unique_ptr<Data> m_Data;
};