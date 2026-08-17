#if defined(FALLOUT4)

#include "Core/Material/Skyrim/HairTintMaterial.h"
#include "Utils/Adapter.h"

HairTintMaterial::HairTintMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<HairTintMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void HairTintMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	reinterpret_cast<Data*>(m_Data.get())->TintColor = float3(runtime.tintColor[0], runtime.tintColor[1], runtime.tintColor[2]);
}

#endif
