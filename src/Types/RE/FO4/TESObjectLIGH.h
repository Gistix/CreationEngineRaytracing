#pragma once

#if defined(FALLOUT4)

#include <cstdint>

namespace RE
{
	enum class TES_LIGHT_FLAGS : std::uint32_t
	{
		kNone = 0,
		kDynamic = 1 << 0,          // 0x00000001
		kCanCarry = 1 << 1,         // 0x00000002
		kNegative = 1 << 2,         // 0x00000004
		kFlicker = 1 << 3,          // 0x00000008
		kDeepCopy = 1 << 4,         // 0x00000010
		kOffByDefault = 1 << 5,     // 0x00000020
		kFlickerSlow = 1 << 6,      // 0x00000040
		kPulse = 1 << 7,            // 0x00000080
		kPulseSlow = 1 << 8,        // 0x00000100
		kSpotShadow = 1 << 10,      // 0x00000400
		kHemiShadow = 1 << 11,      // 0x00000800
		kOmniShadow = 1 << 12,      // 0x00001000
		kPortalStrict = 1 << 13,    // 0x00002000
		kSpotlight = 1 << 14,       // 0x00004000
		kAffectsLand = 1 << 15,     // 0x00008000
		kAffectsWater = 1 << 16,    // 0x00010000
		kNeverFades = 1 << 18,      // 0x00040000
		kVolumetric = 1 << 20,      // 0x00100000

		kType = kSpotlight | kSpotShadow | kHemiShadow | kOmniShadow
	};
}

#endif
