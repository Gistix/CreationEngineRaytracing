#include "Core/Material/Skyrim/FacegenTintMaterial.h"

#include "Util.h"

FacegenTintMaterial::FacegenTintMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;

	m_Data = eastl::make_unique<FacegenTintMaterialData>();

	Initialize(m_Data.get(), shaderMaterial);
}

void FacegenTintMaterial::Initialize(MaterialBase::Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::Initialize(data, shaderMaterial);

	auto tintData = reinterpret_cast<Data*>(data);

	auto tintMaterial = skyrim_cast<RE::BSLightingShaderMaterialFacegenTint*>(shaderMaterial);
	if (!tintMaterial) {
		logger::error("FacegenTintMaterial::Initialize - Shader material is not BSLightingShaderMaterialFacegenTint");
		return;
	}

	tintData->TintColor = Util::Math::Float3(tintMaterial->tintColor);
}
