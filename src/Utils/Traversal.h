#pragma once

#include "Constants.h"
#include <eastl/unordered_set.h>

#if defined(FALLOUT4)
#include "Types/RE/FO4/NiSwitchNode.h"
#endif

namespace Util
{
	namespace Traversal
	{
		template <bool AllowFork = false, typename LeafFunc, typename ForkFunc = std::nullptr_t>
		static CESEAdapter::RE::BSVisitControl SceneGraphTriShapes(
			RE::NiAVObject* a_object,
			LeafFunc&& a_leafFunc,
			RE::TESObjectREFR* parentRefr = nullptr,
			RE::NiAVObject* firstPersonRoot = nullptr,
			ForkFunc&& a_forkFunc = nullptr)
		{
			auto result = CESEAdapter::RE::BSVisitControl::kContinue;

			if (!a_object)
				return result;

			const bool isVisibleFP = (a_object == firstPersonRoot);
			if (!isVisibleFP && Util::Adapter::IsNiAVObjectHidden(a_object))
				return result;

			// Early return for TriShapes — most common actionable leaf
			if (auto geom = Util::Adapter::AsTriShape(a_object)) {
				if constexpr (std::is_invocable_r_v<CESEAdapter::RE::BSVisitControl, LeafFunc, RE::BSTriShape*, RE::TESObjectREFR*>) {
					return a_leafFunc(geom, parentRefr);
				} else {
					a_leafFunc(geom, parentRefr);
					return CESEAdapter::RE::BSVisitControl::kContinue;
				}
			}

			auto rtti = a_object->GetRTTI();

			if (rtti == Constants::rtti::NiBillboardNode.get() || rtti == Constants::rtti::BSOrderedNode.get())
				return result;

			// Only nodes can have children or be FadeNodes
			auto node = Util::Adapter::AsNode(a_object);
			if (!node)
				return result;

			if (auto switchNode = Util::Adapter::AsSwitchNode(node)) {
				auto index = static_cast<uint16_t>(switchNode->index);
				auto childAt = Util::Adapter::GetChildAt(node, index);
				if (childAt)
					result = SceneGraphTriShapes<AllowFork>(childAt, a_leafFunc, parentRefr, firstPersonRoot, a_forkFunc);
				return result;
			}

			// Propagate owner refr through FadeNodes
			auto refr = parentRefr;
			if (rtti == Constants::rtti::BSFadeNode.get()) {
				if (auto owner = Util::Adapter::GetOwner(a_object))
					refr = owner;
			}

			eastl::vector<RE::NiAVObject*> portalChildren;
			if (rtti == Constants::rtti::ShadowSceneNode.get()) {
				Util::Adapter::GetAlwaysRenderChildren(node, portalChildren);
			}

			auto& children = Util::Adapter::GetChildren(node);

			if constexpr (AllowFork) {
				if (children.size() >= Constants::ParallelTraversalFanoutThreshold) {
					a_forkFunc(node, refr, portalChildren);
					return result;
				}
			}

			for (auto& child : children) {
				if (child) {
					result = SceneGraphTriShapes<AllowFork>(child.get(), a_leafFunc, refr, firstPersonRoot, a_forkFunc);
					if (result == CESEAdapter::RE::BSVisitControl::kStop)
						return result;
				}
			}

			for (auto* child : portalChildren) {
				if (child) {
					result = SceneGraphTriShapes<AllowFork>(child, a_leafFunc, refr, firstPersonRoot, a_forkFunc);
					if (result == CESEAdapter::RE::BSVisitControl::kStop)
						return result;
				}
			}

			return result;
		}

		template <typename Func>
		static CESEAdapter::RE::BSVisitControl ScenegraphTriShapes(
			RE::NiAVObject* a_object, 
			Func&& a_func,
			RE::TESObjectREFR* parentRefr = nullptr,
			RE::NiAVObject* firstPersonRoot = nullptr)
		{
			return SceneGraphTriShapes<false>(a_object, std::forward<Func>(a_func), parentRefr, firstPersonRoot);
		}
	}
}