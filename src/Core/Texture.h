#pragma once

#include <PCH.h>
#include "Framework/DescriptorTableManager.h"

struct Texture
{
	eastl::weak_ptr<DescriptorHandle> texture;
	DescriptorHandle* defaultTexture;

	uint16_t GetDescriptorIndex() const
	{
		auto locked = texture.lock();

		if (locked)
			return static_cast<uint16_t>(locked->Get());
		else
			return static_cast<uint16_t>(defaultTexture->Get());
	}
};