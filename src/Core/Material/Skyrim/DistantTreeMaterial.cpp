#include "Core/Material/Skyrim/DistantTreeMaterial.h"

#include "Core/MaterialManager.h"
#include "Renderer.h"
#include "Util.h"
#include "Scene.h"

DistantTreeMaterial::DistantTreeMaterial(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<Data>();

	UpdateData(shaderMaterial);
	UpdateTextures(shaderMaterial);
}

void DistantTreeMaterial::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	auto data = reinterpret_cast<Data*>(m_Data.get());

	data->Type = MaterialBase::Type::DistantTree;
	data->Feature = static_cast<uint16_t>(shaderMaterial->GetFeature());

	data->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	data->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);
}

void DistantTreeMaterial::UpdateTextures([[ maybe_unused ]] RE::BSShaderMaterial* shaderMaterial)
{
	auto renderer = Renderer::GetSingleton();

	auto data = reinterpret_cast<Data*>(m_Data.get());

	// TODO: Replace by a single MaterialTexture defined in Scene
	if (m_TreeLODAtlasTexture.Update(Scene::GetSingleton()->g_TreeLODAtlasTex->get(), renderer->GetBlackTextureDescriptor()))
		data->TreeLODAtlas = m_TreeLODAtlasTexture.texture.GetDescriptorIndex();
}
