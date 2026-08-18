#if defined(FALLOUT4)

#include "Core/Material/Fallout4/FacegenTintMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

FacegenTintMaterial::FacegenTintMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<FacegenTintMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void FacegenTintMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialSkinTint*>(shaderMaterial);
	data->TintColor = { mat->tintColor.r, mat->tintColor.g, mat->tintColor.b };
}

#endif
