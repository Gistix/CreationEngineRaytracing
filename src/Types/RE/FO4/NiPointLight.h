#pragma once

#include "RE/N/NiLight.h"

namespace RE
{
	class NiPointLight : public NiLight
	{
	public:
		static constexpr auto RTTI{ RTTI::NiPointLight };
		static constexpr auto VTABLE{ VTABLE::NiPointLight };
		static constexpr auto Ni_RTTI{ Ni_RTTI::NiPointLight };

		float constantAttenuation;   // 170
		float linearAttenuation;     // 174
		float quadraticAttenuation;  // 178
		std::uint8_t pad17C[0x14];  // 17C
	};
#if defined(SKYRIM)
	static_assert(sizeof(NiPointLight) == 0x190);
#endif
}
