#pragma once

#include <PCH.h>

#include "Core/Texture.h"
#include "Core/TextureManager.h"

struct MaterialTexture
{
	Texture texture;
	RE::NiTexture* sourceTexture = nullptr;

	bool Update(const RE::NiPointer<RE::NiSourceTexture>& a_sourceTexture, const eastl::shared_ptr<DescriptorHandle> a_defaultDescriptor, TextureType a_type = TextureType::Standard);
	bool Update(RE::NiTexture* a_sourceTexture, const eastl::shared_ptr<DescriptorHandle> a_defaultDescriptor, TextureType a_type = TextureType::Standard);
};
