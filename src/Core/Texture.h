#pragma once

#include <PCH.h>
#include "Framework/DescriptorTableManager.h"

struct Texture
{
	eastl::weak_ptr<DescriptorHandle> texture;
	DescriptorHandle* defaultTexture;
};