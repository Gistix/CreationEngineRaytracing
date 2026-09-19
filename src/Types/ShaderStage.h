#pragma once

#include <cstdint>

enum class ShaderStage : uint8_t
{
	Library,
	Compute,
	Vertex,
	Pixel,
	Geometry,
	Hull,
	Domain,
	Mesh,
	Amplification
};
