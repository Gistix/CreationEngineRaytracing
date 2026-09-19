#pragma once

#include "Core/Material/MaterialBase.h"
#include "Core/MaterialTexture.h"
#include "Interop/Material/Skyrim/DistantTreeMaterialData.hlsli"

struct DistantTreeMaterial : public MaterialBase
{
	using Data = DistantTreeMaterialData;

	DistantTreeMaterial() = default;

	DistantTreeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_TreeLODAtlasTexture;
};
