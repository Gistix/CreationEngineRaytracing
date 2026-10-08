#pragma once

#include <PCH.h>
#include "Framework/DescriptorTableManager.h"

struct Texture
{
	eastl::weak_ptr<DescriptorHandle> texture;
	DescriptorHandle* defaultTexture = nullptr;
	uint32_t width = 1;
	uint32_t height = 1;
	float lodBias = 0.0f;

	Texture() = default;

	Texture(const eastl::shared_ptr<DescriptorHandle>& a_texture, DescriptorHandle* a_defaultTexture, uint32_t a_width = 1, uint32_t a_height = 1)
		: texture(a_texture), defaultTexture(a_defaultTexture), width(a_width), height(a_height)
	{
		lodBias = 0.5f * std::log2(std::max(1.0f, static_cast<float>(width) * static_cast<float>(height)));
	}

	uint16_t GetDescriptorIndex() const
	{
		auto locked = texture.lock();

		if (locked)
			return static_cast<uint16_t>(locked->Get());
		else
			return static_cast<uint16_t>(defaultTexture->Get());
	}

	float GetLODBias() const
	{
		return lodBias;
	}
};
