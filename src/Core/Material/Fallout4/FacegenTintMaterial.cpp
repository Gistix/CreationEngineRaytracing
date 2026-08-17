#if defined(FALLOUT4)

#include "Core/Material/Skyrim/FacegenTintMaterial.h"
#include "Utils/Adapter.h"

FacegenTintMaterial::FacegenTintMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<FacegenTintMaterialData>();
	UpdateData(shaderMaterial);
}

void FacegenTintMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	data->TintColor = float3(runtime.tintColor[0], runtime.tintColor[1], runtime.tintColor[2]);
}

#endif
