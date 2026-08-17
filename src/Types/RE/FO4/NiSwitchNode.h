#pragma once

#if defined(FALLOUT4)

#include "RE/N/NiNode.h"

namespace RE
{
	class NiSwitchNode : public NiNode
	{
	public:
		inline static constexpr auto RTTI{ RTTI::NiSwitchNode };
		inline static constexpr auto VTABLE{ VTABLE::NiSwitchNode };

		// members
		std::uint16_t                    flags;       // 140
		std::uint16_t                    pad142;      // 142
		std::int32_t                     index;       // 144
		float                            savedTime;   // 148
		std::uint32_t                    revID;       // 14C
	};
}

#endif
