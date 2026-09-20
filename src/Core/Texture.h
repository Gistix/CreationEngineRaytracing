#pragma once

#include <PCH.h>
#include "Framework/DescriptorTableManager.h"

struct Texture
{
	eastl::shared_ptr<DescriptorHandle> texture;
	DescriptorHandle* defaultTexture;

	uint16_t GetDescriptorIndex() const
	{
		if (texture)
			return static_cast<uint16_t>(texture->Get());
		else
			return static_cast<uint16_t>(defaultTexture->Get());
	}
};
