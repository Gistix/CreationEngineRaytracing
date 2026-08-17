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
		// TODO: verify exact offset of portalGraph in Fallout 4
		// In Skyrim, NiNode size is 0x128, portalGraph is at 0x228 (+0x100)
		// In FO4, NiNode size is 0x140, so we assume portalGraph might be around 0x240
		std::uint64_t pad140[(0x240 - 0x140) / 8]; // 140
		BSPortalGraph* portalGraph;                // 240
	};
}

#endif
