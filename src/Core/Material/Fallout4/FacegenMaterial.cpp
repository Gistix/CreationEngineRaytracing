#if defined(FALLOUT4)

#include "Core/Material/Fallout4/FacegenMaterial.h"
#include "Renderer.h"
#include "Types/RE/FO4/BSLightingShaderMaterials.h"

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
	auto* renderer = Renderer::GetSingleton();
	auto* data = reinterpret_cast<Data*>(m_Data.get());
    auto* mat = static_cast<RE::BSLightingShaderMaterialFace*>(shaderMaterial);
	if (m_FaceTexture.Update(mat->faceTexture.get(), renderer->GetWhiteTextureDescriptor()))
		data->FaceTexture = m_FaceTexture.texture.GetDescriptorIndex();
}

#endif
