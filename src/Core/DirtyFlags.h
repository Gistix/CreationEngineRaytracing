#pragma once

enum class DirtyFlags : uint8_t
{
	None = 0,
	Transform = 1 << 0,
	Skin = 1 << 1,
	Vertex = 1 << 2,
	Visibility = 1 << 3
};

DEFINE_ENUM_FLAG_OPERATORS(DirtyFlags);