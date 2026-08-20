#pragma once

#if defined(FALLOUT4)

#include "RE/N/NiNode.h"
#include "RE/B/BSTArray.h"
#include "Types/RE/FO4/BSLight.h"

namespace RE
{
	class BSPortalGraph;

	class ShadowSceneNode : public NiNode
	{
	public:
		inline static constexpr auto RTTI{ RTTI::ShadowSceneNode };
		inline static constexpr auto VTABLE{ VTABLE::ShadowSceneNode };

		// members
		BSTArray<BSLight*> activeLights;                  // 140
		BSTArray<BSLight*> activeShadowLights;            // 158
		BSTArray<BSLight*> activePointLights;             // 170
		BSTArray<BSLight*> activeShadowPointLights;       // 188
		BSTArray<BSLight*> activeDirectionalLights;       // 1A0
		BSTArray<BSLight*> activeShadowDirectionalLights; // 1B8
		NiPointer<BSLight> sunLight;                      // 1D0
		NiPointer<BSLight> ambientLight;                  // 1D8
		std::uint8_t       pad1E0[0x58];                  // 1E0
		BSPortalGraph*     portalGraph;                   // 238
	};
	static_assert(offsetof(ShadowSceneNode, portalGraph) == 0x238);
	static_assert(offsetof(ShadowSceneNode, activeLights) == 0x140);
	static_assert(offsetof(ShadowSceneNode, sunLight) == 0x1D0);
}

#endif
