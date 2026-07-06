#include "Core/Material/Skyrim/HairTintMaterial.h"

#include "Util.h"

HairTintMaterial::HairTintMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<HairTintMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void HairTintMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto tintData = reinterpret_cast<Data*>(data);

	auto tintMaterial = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderMaterial);
	if (!tintMaterial) {
		logger::error("HairTintMaterial::Initialize - Shader material is not BSLightingShaderMaterialHairTint");
		return;
	}

	tintData->TintColor = Util::Math::Float3(tintMaterial->tintColor);
}
