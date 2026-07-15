#include "Core/Material/Skyrim/HairTintMaterial.h"

#include "Util.h"

HairTintMaterial::HairTintMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<HairTintMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void HairTintMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);

	auto tintMaterial = reinterpret_cast<RE::BSLightingShaderMaterialHairTint*>(shaderMaterial);

	auto tintData = reinterpret_cast<Data*>(m_Data.get());
	tintData->TintColor = Util::Math::Float3(tintMaterial->tintColor);
}
