#pragma once

#if defined(FALLOUT4)

#include "RE/N/NiExtraData.h"

namespace RE
{
	class NiIntegersExtraData : public NiExtraData
	{
	public:
		inline static constexpr auto RTTI{ RTTI::NiIntegersExtraData };
		inline static constexpr auto VTABLE{ VTABLE::NiIntegersExtraData };
		inline static constexpr auto Ni_RTTI{ Ni_RTTI::NiIntegersExtraData };

		virtual ~NiIntegersExtraData(); // 00

		// members
		std::uint32_t size;   // 18
		std::uint32_t pad1C;  // 1C
		std::int32_t* value;  // 20
	};
	static_assert(sizeof(NiIntegersExtraData) == 0x28);
}

#endif
