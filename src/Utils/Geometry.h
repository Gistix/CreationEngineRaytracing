#pragma once

#include "Constants.h"

struct Mesh;

namespace Util
{
	namespace Geometry
	{
		std::uint16_t GetSkyrimVertexSize(RE::BSGraphics::Vertex::Flags flags);

		uint16_t GetStoredVertexSize(RE::BSGraphics::VertexDesc desc);

		bool IsDismemberSkinInstance(void* skinInstance);

		void GetDismemberPartitionVisibility(void* skinInstance, eastl::vector<bool>& outVisibility);

		bool HasDoubleSidedGeom(Mesh* mesh);

		bool IsBlocklisted(const char* name);
	}
}