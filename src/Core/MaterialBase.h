#pragma once

#include <PCH.h>

#include "Core/Texture.h"
#include "Core/TextureManager.h"

struct MaterialBase
{
	static Texture GetTexture(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, TextureType textureType = TextureType::Standard);
};