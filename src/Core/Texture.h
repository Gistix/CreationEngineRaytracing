#pragma once

#include <PCH.h>
#include "Framework/DescriptorTableManager.h"

struct Texture
{
	eastl::shared_ptr<DescriptorHandle> texture;
	DescriptorHandle* defaultTexture = nullptr;
	uint32_t width = 1;
	uint32_t height = 1;
	float lodBias = 0.0f;

	Texture() = default;

	Texture(eastl::shared_ptr<DescriptorHandle> a_texture, DescriptorHandle* a_defaultTexture, uint32_t a_width = 1, uint32_t a_height = 1)
		: texture(std::move(a_texture)), defaultTexture(a_defaultTexture), width(a_width), height(a_height)
	{
		lodBias = 0.5f * std::log2(std::max(1.0f, static_cast<float>(width) * static_cast<float>(height)));
	}

	uint16_t GetDescriptorIndex() const
	{
		if (texture)
			return static_cast<uint16_t>(texture->Get());
		else
			return static_cast<uint16_t>(defaultTexture->Get());
	}

	float GetLODBias() const
	{
		return lodBias;
	}
};
