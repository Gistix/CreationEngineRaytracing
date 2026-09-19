#pragma once

namespace RE
{
	enum class NiAlphaPropertyFlags : uint16_t
	{
		kNone = 0,
		kAlphaBlend = 1 << 0,             // 0x0001
		kSrcBlendMode = 0xF << 1,         // 0x001E
		kDestBlendMode = 0xF << 5,        // 0x01E0
		kAlphaTest = 1 << 9,              // 0x0200
		kTestMode = 0xF << 10,            // 0x3C00
		kNoSmooth = 1 << 14,              // 0x4000
		kPad = 1 << 15                    // 0x8000
	};
}