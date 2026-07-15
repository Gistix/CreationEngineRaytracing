#pragma once

#include <PCH.h>

#include "Core/Texture.h"
#include "Core/TextureManager.h"

struct MaterialTexture
{
	Texture texture;
	RE::NiSourceTexture* sourceTexture = nullptr;

	bool Update(const RE::NiSourceTexturePtr& a_sourceTexture, const eastl::shared_ptr<DescriptorHandle> a_defaultDescriptor, TextureType a_type = TextureType::Standard);
};