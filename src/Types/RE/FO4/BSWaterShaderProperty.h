#pragma once

#include "RE/N/NiPlane.h"
#include "RE/B/BSTArray.h"

namespace RE
{
	class BSWaterShaderProperty : public BSShaderProperty
	{
	public:
		static constexpr auto RTTI{ RTTI::BSWaterShaderProperty };
		static constexpr auto VTABLE{ VTABLE::BSWaterShaderProperty };
		static constexpr auto Ni_RTTI{ Ni_RTTI::BSWaterShaderProperty };

		~BSWaterShaderProperty() override;

		// members
		std::uint32_t waterFlags;     // 70
		std::uint32_t                               unk74;          // 74
		std::int32_t                                cellX;          // 78
		std::int32_t                                cellY;          // 7C
		std::int32_t                                flowX;          // 80
		std::int32_t                                flowY;          // 84
		NiPlane                                     plane;          // 88
		std::uint64_t                               unk98;          // 98
		std::uint8_t                                unkA0;          // A0
	};
	static_assert(sizeof(BSWaterShaderProperty) == 0xA8);
}