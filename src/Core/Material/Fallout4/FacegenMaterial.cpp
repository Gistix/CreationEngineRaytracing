#if defined(FALLOUT4)

#include "Core/Material/Skyrim/FacegenMaterial.h"
#include "Renderer.h"
#include "Utils/Adapter.h"

FacegenMaterial::FacegenMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	Initialize(shaderMaterial, offset);
	m_Data = eastl::make_unique<FacegenMaterialData>();
	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void FacegenMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateData(shaderMaterial);
}

void FacegenMaterial::UpdateTextures(RE::BSShaderMaterial* shaderMaterial)
{
	LightingMaterial::UpdateTextures(shaderMaterial);
	const auto runtime = Util::Adapter::GetMaterialRuntimeData(shaderMaterial);
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
	if (m_TintTexture.Update(runtime.faceTexture, renderer->GetWhiteTextureDescriptor()))
		data->TintTexture = m_TintTexture.texture.GetDescriptorIndex();
}

#endif
