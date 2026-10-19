#pragma once

#if defined(FALLOUT4)

#include "RE/N/NiExtraData.h"

namespace RE
{
	class NiBinaryExtraData : public NiExtraData
	{
	public:
		inline static constexpr auto RTTI{ RTTI::NiBinaryExtraData };
		inline static constexpr auto VTABLE{ VTABLE::NiBinaryExtraData };
		inline static constexpr auto Ni_RTTI{ Ni_RTTI::NiBinaryExtraData };

		~NiBinaryExtraData() override = default;

		std::uint8_t* data{ nullptr };  // 0x18
		std::uint32_t size{ 0 };         // 0x20
	};
	static_assert(sizeof(NiBinaryExtraData) == 0x28);
}

#endif
