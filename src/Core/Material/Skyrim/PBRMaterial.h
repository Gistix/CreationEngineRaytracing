#pragma once

#include "Core/Material/MaterialBase.h"
#include "Core/MaterialTexture.h"
#include "Interop/Material/Skyrim/PBRMaterialData.hlsli"

struct PBRMaterial : public MaterialBase
{
	using Data = PBRMaterialData;

	PBRMaterial() = default;

	PBRMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	void UpdateData(RE::BSShaderMaterial* shaderMaterial) override;

	void UpdateTextures(RE::BSShaderMaterial* shaderMaterial) override;

	virtual MaterialBase::Data* GetData() override { return m_Data.get(); }

	virtual size_t GetDataSize() override { return sizeof(Data); }

	MaterialTexture m_DiffuseTexture;
	MaterialTexture m_NormalTexture;
	MaterialTexture m_RimSoftLightingTexture;
	MaterialTexture m_SpecularBackLightingTexture;
	MaterialTexture m_RMAOSTexture;
	MaterialTexture m_EmissiveTexture;
	MaterialTexture m_FeaturesTexture0;
	MaterialTexture m_FeaturesTexture1;
};
