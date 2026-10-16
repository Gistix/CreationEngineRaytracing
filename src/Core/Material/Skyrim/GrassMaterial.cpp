#include "Core/Material/Skyrim/GrassMaterial.h"

GrassMaterial::GrassMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<LightingMaterialData>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void GrassMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);

	reinterpret_cast<Data*>(m_Data.get())->Type = MaterialBase::Type::Grass;
}
