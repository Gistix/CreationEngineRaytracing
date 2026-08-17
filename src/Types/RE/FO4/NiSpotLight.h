#pragma once

#include "Types/RE/FO4/NiPointLight.h"

namespace RE
{
	class NiSpotLight : public NiPointLight
	{
	public:
		static constexpr auto RTTI{ RTTI::NiSpotLight };
		static constexpr auto VTABLE{ VTABLE::NiSpotLight };
		static constexpr auto Ni_RTTI{ Ni_RTTI::NiSpotLight };

		float outerSpotAngle;    // 190
		float innerSpotAngle;    // 194
		float spotExponent;      // 198
		std::uint8_t  unk19C[8]; // 19C
		float unk1A4;            // 1A4
		std::uint8_t pad1A8[8];  // 1A8
	};
#if defined(SKYRIM) || defined(FALLOUT4)
	static_assert(sizeof(NiSpotLight) == 0x1B0);
#endif
}
