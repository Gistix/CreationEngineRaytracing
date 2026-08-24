#pragma once

#if defined(FALLOUT4)

#include "RE/N/NiNode.h"
#include "RE/B/BSTArray.h"
#include "RE/N/NiPointer.h"
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
		std::uint8_t                       pad140[0x18];                  // 140
		BSTArray<NiPointer<BSLight>>       activeLights;                  // 158
		BSTArray<NiPointer<BSShadowLight>> activeShadowLights;            // 170
		BSTArray<NiPointer<BSLight>>       activePointLights;             // 188
		BSTArray<NiPointer<BSShadowLight>> activeShadowPointLights;       // 1A0
		BSTArray<NiPointer<BSLight>>       activeDirectionalLights;       // 1B8
		std::uint8_t                       pad1D0[0x8];                   // 1D0
		BSTArray<NiPointer<BSShadowLight>> activeShadowDirectionalLights; // 1D8
		std::uint8_t                       pad1F0[0x8];                   // 1F0
		NiPointer<BSLight>                 sunLight;                      // 1F8
		NiPointer<BSLight>                 cloudSunLight;                 // 200
		NiPointer<BSShadowLight>           shadowSunLight;                // 208
		NiPointer<BSLight>                 localSunLight;                 // 210
		std::uint8_t                       pad218[0x18];                  // 218
		NiPointer<BSLight>                 ambientLight;                  // 230
		BSPortalGraph*                     portalGraph;                   // 238
	};
	static_assert(offsetof(ShadowSceneNode, activeLights) == 0x158);
	static_assert(offsetof(ShadowSceneNode, activeShadowLights) == 0x170);
	static_assert(offsetof(ShadowSceneNode, sunLight) == 0x1F8);
	static_assert(offsetof(ShadowSceneNode, portalGraph) == 0x238);
}

#endif
