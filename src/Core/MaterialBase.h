#pragma once

#include <PCH.h>

#include "Core/Texture.h"
#include "Core/TextureManager.h"

struct MaterialBase
{
	static Texture GetTexture([[maybe_unused]] const RE::NiPointer<RE::NiSourceTexture>& niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, [[maybe_unused]] TextureType textureType = TextureType::Standard);
};