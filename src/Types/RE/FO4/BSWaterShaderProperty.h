#pragma once

#include "RE/B/BSShaderProperty.h"
#include "RE/B/BSTArray.h"
#include "RE/N/NiPlane.h"

namespace RE
{
	class BSWaterShaderProperty : public BSShaderProperty
	{
	public:
		std::uint64_t unk70;                         // 70
		NiPlane       waterPlane;                    // 78
		std::uint64_t unk88;                         // 88
		std::uint64_t unk90;                         // 90
		std::uint32_t unk98;                         // 98
		std::uint8_t  unk9C[8];                      // 9C
		std::uint8_t  padA4[4];                      // A4
		BSTArray<void*> waterFogPasses;              // A8
	};
	static_assert(offsetof(BSWaterShaderProperty, waterFogPasses) == 0xA8);
}
