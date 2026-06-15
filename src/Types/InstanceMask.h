#pragma once

enum InstanceMask : uint8_t
{
	None = 0,
	Default = 1 << 0,
	Water = 1 << 1,
	All = 0xFF
};

DEFINE_ENUM_FLAG_OPERATORS(InstanceMask);