#pragma once

#include <PCH.h>
#include "Framework/DescriptorTableManager.h"

struct Texture
{
	eastl::weak_ptr<DescriptorHandle> texture;
	DescriptorHandle* defaultTexture = nullptr;
	nvrhi::TextureHandle handle = nullptr;
	uint32_t alphaChannel = 3;

	Texture() = default;

	Texture(
		eastl::shared_ptr<DescriptorHandle> textureHandle,
		DescriptorHandle* defaultTextureHandle,
		nvrhi::TextureHandle nativeHandle = nullptr,
		uint32_t alphaChannelIndex = 3) :
		texture(textureHandle),
		defaultTexture(defaultTextureHandle),
		handle(nativeHandle),
		alphaChannel(alphaChannelIndex)
	{
	}
};
