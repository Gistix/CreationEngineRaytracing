#pragma once

#include "Constants.h"

struct Mesh;

namespace Util
{
	namespace Geometry
	{
		std::uint16_t GetSkyrimVertexSize(RE::BSGraphics::Vertex::Flags flags);

		uint16_t GetStoredVertexSize(RE::BSGraphics::VertexDesc desc);

		bool IsDismemberSkinInstance(RE::NiSkinInstance* skinInstance);

		void GetDismemberPartitionVisibility(RE::NiSkinInstance* skinInstance, eastl::vector<uint8_t>& outVisibility);

		bool HasDoubleSidedGeom(Mesh* mesh);

		bool IsBlocklisted(const char* name);
	}
}