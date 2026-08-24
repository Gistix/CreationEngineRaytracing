#pragma once

#include "Constants.h"
#include <nvrhi/nvrhi.h>

namespace Util
{
	namespace Geometry
	{
		std::uint16_t GetSkyrimVertexSize(RE::BSGraphics::Vertex::Flags flags);

		uint16_t GetStoredVertexSize(RE::BSGraphics::VertexDesc desc);

		nvrhi::Format GetVertexPositionFormat(RE::BSGraphics::VertexDesc desc);

		bool IsDismemberSkinInstance(RE::NiObject* skinInstance);

		void GetDismemberPartitionVisibility(RE::NiObject* skinInstance, eastl::vector<bool>& outVisibility);

		bool IsBlocklisted(const char* name);
	}
}