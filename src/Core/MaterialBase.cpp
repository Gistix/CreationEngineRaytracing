#include "Core/MaterialBase.h"

#include "Core/TextureManager.h"
#include "Scene.h"

Texture MaterialBase::GetTexture([[maybe_unused]] const RE::NiPointer<RE::NiSourceTexture>& niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, [[maybe_unused]] TextureType textureType)
{
#if defined(SKYRIM)
	if (!niPointer || !niPointer->rendererTexture)
		return Texture(defaultDescHandle, nullptr);

	auto& textureManager = Scene::GetSingleton()->GetSceneGraph()->GetTextureManager();

	if (auto result = textureManager->GetDescriptor(niPointer->rendererTexture, textureType))
		return Texture(result, defaultDescHandle.get());
#endif
	return Texture(defaultDescHandle, nullptr);

}