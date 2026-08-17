#pragma once

#if defined(FALLOUT4)

#include "RE/N/NiNode.h"

namespace RE
{
	class BSPortalGraph;

	class ShadowSceneNode : public NiNode
	{
	public:
		inline static constexpr auto RTTI{ RTTI::ShadowSceneNode };
		inline static constexpr auto VTABLE{ VTABLE::ShadowSceneNode };

		// members
		std::uint8_t  pad140[0xF8];                // 140
		BSPortalGraph* portalGraph;                // 238
	};
	static_assert(offsetof(ShadowSceneNode, portalGraph) == 0x238);
}

#endif
