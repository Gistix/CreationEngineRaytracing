#include "Core/Material/Skyrim/LightingMaterial.h"

#include "Util.h"

LightingMaterial::LightingMaterial(RE::BSShaderMaterial* shaderMaterial)
{
	m_Data = eastl::make_unique<LightingMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void LightingMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	MaterialBase::Initialize(data, shaderMaterial);

	auto lightingData = reinterpret_cast<Data*>(data);

	auto lightingShaderMaterial = skyrim_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial);
	if (!lightingShaderMaterial) {
		logger::error("LightingMaterial::Initialize - Shader material is not BSLightingShaderMaterialBase");
		return;
	}

	lightingData->SpecularColor = Util::Math::Float3(lightingShaderMaterial->specularColor) * lightingShaderMaterial->specularColorScale;
	lightingData->SpecularPower = lightingShaderMaterial->specularPower;
}