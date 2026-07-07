#pragma once

#include "Core/Material/MaterialBase.h"
#include "Core/Texture.h"
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

	Texture m_DiffuseTexture;
	Texture m_NormalTexture;
	Texture m_RimSoftLightingTexture;
	Texture m_SpecularBackLightingTexture;
	Texture m_RMAOSTexture;
	Texture m_EmissiveTexture;
	Texture m_FeaturesTexture0;
	Texture m_FeaturesTexture1;
};
