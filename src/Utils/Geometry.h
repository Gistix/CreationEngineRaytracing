#pragma once

#include "Constants.h"

struct Mesh;

namespace Util
{
	namespace Geometry
	{
		std::uint32_t GetSkyrimVertexSize(RE::BSGraphics::Vertex::Flags flags);

		uint16_t GetStoredVertexSize(uint64_t desc);

		bool HasDoubleSidedGeom(Mesh* mesh);
	}
}