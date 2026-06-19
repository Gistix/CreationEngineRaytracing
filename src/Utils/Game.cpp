#include "Game.h"
#include "Utils/Adapter.h"

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
			static float& cameraNear = *(float*)REL::ID(57985).address();
			static float& cameraFar = *(float*)REL::ID(958877).address();
#endif

			float4 cameraData{};
			cameraData.x = cameraFar;
			cameraData.y = cameraNear;
			cameraData.z = cameraFar - cameraNear;
			cameraData.w = cameraFar * cameraNear;

			return cameraData;
		}

#if defined(SKYRIM)
		bool IsGlobalLight(RE::BSLight* a_light)
		{
#if defined(SKYRIM)
			return !(a_light->portalStrict || !a_light->portalGraph);
#elif defined(FALLOUT4)
			return true;
#endif
		}
#elif defined(FALLOUT4)
		bool IsGlobalLight([[maybe_unused]] RE::BSLight* a_light)
		{
			// FO4 BSLight does not have portalStrict/portalGraph members
			return true;
		}
#endif

		bool IsHidden(RE::NiAVObject* object, RE::NiAVObject* root) {
			if (!object)
				return false;

			if (Util::Adapter::IsNiAVObjectHidden(object))
				return true;

			if (auto* multiBoundNode = netimmerse_cast<RE::BSMultiBoundNode*>(object)) {
				if (Util::Adapter::IsMultiBoundNodeAllFail(multiBoundNode))
					return true;
			}

			if (!object->parent) {
				// If the last node is not the WorldRoot Node, the object is detached
				if (object != RE::Main::GetSingleton()->WorldRootNode())
					return true;

				return false;
			}

#if defined(SKYRIM)
			// If object parent index is not active on its parent NiSwitchNode, it is effectively hidden
			if (auto* switchNode = netimmerse_cast<RE::NiSwitchNode*>(object->parent)) {
				// This looks backwards, but its the way it seem to work
				if (static_cast<uint32_t>(switchNode->index) == object->parentIndex)
					return true;
			}
#endif

			// Stop recursion if parent is the root (do not check the root)
			if (object->parent == root)
				return false;

			return IsHidden(object->parent, root);
		}

		RE::NiCamera* FindNiCamera(RE::NiAVObject* object)
		{
			if (auto* camera = skyrim_cast<RE::NiCamera*>(object))
				return camera;

			auto* node = object->AsNode();
			if (!node)
				return nullptr;

			for (auto& child : node->GetChildren()) {
				if (child) {
					if (auto* res = FindNiCamera(child.get()))
						return res;
				}
			}
			return nullptr;
		}
	}
}