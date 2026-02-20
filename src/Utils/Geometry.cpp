#pragma once

#include "Geometry.h"

namespace Util
{
	namespace Geometry
	{
		std::uint32_t GetSkyrimVertexSize(RE::BSGraphics::Vertex::Flags flags)
		{
			using RE::BSGraphics::Vertex;

			std::uint32_t vertexSize = 0;

			if (flags & Vertex::VF_VERTEX) {
				vertexSize += sizeof(float) * 4;
			}
			if (flags & Vertex::VF_UV) {
				vertexSize += sizeof(std::uint16_t) * 2;
			}
			if (flags & Vertex::VF_UV_2) {
				vertexSize += sizeof(std::uint16_t) * 2;
			}
			if (flags & Vertex::VF_NORMAL) {
				vertexSize += sizeof(std::uint16_t) * 2;
				if (flags & Vertex::VF_TANGENT) {
					vertexSize += sizeof(std::uint16_t) * 2;
				}
			}
			if (flags & Vertex::VF_COLORS) {
				vertexSize += sizeof(std::uint8_t) * 4;
			}
			if (flags & Vertex::VF_SKINNED) {
				vertexSize += sizeof(std::uint16_t) * 4 + sizeof(std::uint8_t) * 4;
			}
			if (flags & Vertex::VF_EYEDATA) {
				vertexSize += sizeof(float);
			}
			if (flags & Vertex::VF_LANDDATA) {
				vertexSize += sizeof(uint32_t) * 2;
			}

			return vertexSize;
		}

		uint16_t GetStoredVertexSize(uint64_t desc)
		{
			return (desc & 0xF) * 4;
		}
	}
}