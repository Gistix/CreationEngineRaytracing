#include "Core/MaterialBase.h"

#include "Core/TextureManager.h"
#include "Scene.h"

Texture MaterialBase::GetTexture(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, TextureType textureType)
{
	if (!niPointer || !niPointer->rendererTexture)
		return Texture(defaultDescHandle, nullptr);

	auto& textureManager = Scene::GetSingleton()->GetSceneGraph()->GetTextureManager();

	if (auto result = textureManager->GetDescriptor(niPointer->rendererTexture, textureType))
		return Texture(result, defaultDescHandle.get());

	return Texture(defaultDescHandle, nullptr);
}