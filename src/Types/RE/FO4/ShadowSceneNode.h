#pragma once

#include "RE/N/NiNode.h"

namespace RE
{
	class BSPortalGraph;

	class ShadowSceneNode : public NiNode
	{
	public:
		static constexpr auto RTTI{ RTTI::ShadowSceneNode };
		static constexpr auto VTABLE{ VTABLE::ShadowSceneNode };
		static constexpr auto Ni_RTTI{ Ni_RTTI::ShadowSceneNode };

		~ShadowSceneNode() override;
		const NiRTTI* GetRTTI() const override;

		struct RuntimeData
		{
			BSPortalGraph* portalGraph;
		};

		RuntimeData& GetRuntimeData()
		{
			return *reinterpret_cast<RuntimeData*>(reinterpret_cast<std::uintptr_t>(this) + 0x238);
		}
	};
}
