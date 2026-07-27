#pragma once

#include "RE/N/NiNode.h"
#include "RE/B/BSTArray.h"

namespace RE
{
	class BSLight;
	class BSShadowDirectionalLight;
	class BSPortalGraph;
	class BSFogProperty;

	class ShadowSceneNode : public NiNode
	{
	public:
		static constexpr auto RTTI{ RTTI::ShadowSceneNode };
		static constexpr auto VTABLE{ VTABLE::ShadowSceneNode };
		static constexpr auto Ni_RTTI{ Ni_RTTI::ShadowSceneNode };

		~ShadowSceneNode() override;
		const NiRTTI* GetRTTI() const override;
		void OnVisible(NiCullingProcess& a_process) override;

		// 0x000-0x140: NiNode

		std::uint64_t                          unk140;                  // 0x140
		std::uint64_t                          unk148;                  // 0x148
		std::uint64_t                          unk150;                  // 0x150
		BSTArray<NiPointer<BSLight>>           activeLights;            // 0x158
		BSTArray<NiPointer<BSShadowLight>>     activeShadowLights;      // 0x170
		BSTArray<NiPointer<BSLight>>           lightQueueAdd;           // 0x188
		BSTArray<NiPointer<BSLight>>           lightQueueRemove;        // 0x1A0
		BSTArray<NiPointer<BSLight>>           unk1B8;                  // 0x1B8
		std::uint32_t                          unk1D0;                  // 0x1D0
		std::uint32_t                          unk1D4;                  // 0x1D4
		BSTArray<NiPointer<NiAVObject>>        decalArray;              // 0x1D8
		mutable std::uint32_t                  decalArrayLock;          // 0x1F0
		std::uint32_t                          pad1F4;                  // 0x1F4
		BSLight*                               sunLight;                // 0x1F8
		BSLight*                               cloudLight;              // 0x200
		BSShadowDirectionalLight*              sunShadowDirLight;       // 0x208
		BSLight*                               localSunLight;           // 0x210
		std::uint8_t                           sceneGraphIndex;         // 0x218
		bool                                   disableLightUpdate;      // 0x219
		bool                                   wireframe;               // 0x21A
		bool                                   opaqueWireframe;         // 0x21B
		std::uint32_t                          pad21C;                  // 0x21C
		std::uint32_t                          unk220;                  // 0x220
		std::uint8_t                           unk224;                  // 0x224
		std::uint8_t                           unk225;                  // 0x225
		std::uint16_t                          unk226;                  // 0x226
		std::uint8_t                           unk228;                  // 0x228
		std::uint8_t                           pad229;                  // 0x229
		std::uint16_t                          pad22A;                  // 0x22A
		std::uint32_t                          pad22C;                  // 0x22C
		NiPointer<BSFogProperty>               fogProperty;             // 0x230
		BSPortalGraph*                         portalGraph;             // 0x238
		BSTArray<BSShadowLight*>               shadowLightsAccum;       // 0x240
		std::uint32_t                          firstPersonShadowMask;   // 0x258
		std::uint32_t                          unk25C;                  // 0x25C
		std::uint64_t                          unk260;                  // 0x260
		std::uint64_t                          unk268;                  // 0x268
		std::uint64_t                          unk270;                  // 0x270
		std::uint64_t                          unk278;                  // 0x278
		std::uint64_t                          unk280;                  // 0x280
		std::uint64_t                          unk288;                  // 0x288
		std::uint64_t                          unk290;                  // 0x290
		std::uint64_t                          unk298;                  // 0x298
		std::uint64_t                          unk2A0;                  // 0x2A0
		std::uint64_t                          unk2A8;                  // 0x2A8
		std::uint64_t                          unk2B0;                  // 0x2B0
		std::uint64_t                          unk2B8;                  // 0x2B8
		std::uint64_t                          unk2C0;                  // 0x2C0
		std::uint64_t                          unk2C8;                  // 0x2C8
		std::uint64_t                          unk2D0;                  // 0x2D0
		std::uint64_t                          unk2D8;                  // 0x2D8
		std::uint64_t                          unk2E0;                  // 0x2E0
		NiPoint3                               lightingOffset;          // 0x2E8
		NiPoint3                               cameraPos;               // 0x2F4
		std::uint64_t                          unk300;                  // 0x300
		std::uint32_t                          unk308;                  // 0x308
		std::uint32_t                          unk30C;                  // 0x30C - written as two floats in constructor (two halves of a qword)
		std::uint32_t                          unk310;                  // 0x310
		std::uint32_t                          unk314;                  // 0x314
		std::uint8_t                           unk318;                  // 0x318
		std::uint8_t                           pad319;                  // 0x319
		std::uint16_t                          pad31A;                  // 0x31A
		std::uint32_t                          pad31C;                  // 0x31C
		std::uint32_t                          unk320;                  // 0x320
		std::uint32_t                          pad324;                  // 0x324
		float                                  unk328;                  // 0x328
		std::uint32_t                          pad32C;                  // 0x32C
	};
	static_assert(sizeof(ShadowSceneNode) == 0x330);
}
