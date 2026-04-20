#include "Game.h"

namespace Util
{
	namespace Game
	{
		float4 GetClippingData()
		{
#if defined(SKYRIM)
			static float& cameraNear = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x40));
			static float& cameraFar = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x44));
#elif defined(FALLOUT4)
			static float& cameraNear = *(float*)REL::RelocationID(57985, 2712882).address();
			static float& cameraFar = *(float*)REL::RelocationID(958877, 2712883).address();
#endif

			float4 cameraData{};
			cameraData.x = cameraFar;
			cameraData.y = cameraNear;
			cameraData.z = cameraFar - cameraNear;
			cameraData.w = cameraFar * cameraNear;

			return cameraData;
		}

		bool IsGlobalLight(RE::BSLight* a_light)
		{
			return !(a_light->portalStrict || !a_light->portalGraph);
		}

		bool IsHidden(RE::NiAVObject* object, RE::NiAVObject* root) {
			if (!object)
				return false;

			if (object->GetFlags().all(RE::NiAVObject::Flag::kHidden))
				return true;

			if (!object->parent)
				return false;

			// If object parent index is not active on its parent NiSwitchNode, it is effectively hidden
			if (auto* switchNode = netimmerse_cast<RE::NiSwitchNode*>(object->parent)) {
				// This looks backwards, but its the way it seem to work
				if (static_cast<uint32_t>(switchNode->index) == object->parentIndex)
					return true;
			}

			// Stop recursion if parent is the root (do not check the root)
			if (object->parent == root)
				return false;

			return IsHidden(object->parent, root);
		}
	}
}