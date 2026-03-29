#pragma once

#include "Constants.h"

#if defined(SKYRIM)
#	include "Types\CommunityShaders\BSLightingShaderMaterialPBR.h"
#endif

namespace Util
{
#if defined(SKYRIM)
	namespace Material::Skyrim
	{
		float ShininessToRoughness(float shininess);

		stl::enumeration<PBRShaderFlags, uint32_t> GetPBRShaderFlags(const BSLightingShaderMaterialPBR* pbrMaterial);
	}
#elif defined(FALLOUT4)
#endif
}