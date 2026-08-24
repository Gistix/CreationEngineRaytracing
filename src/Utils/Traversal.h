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

		static CESEAdapter::RE::BSVisitControl ScenegraphFadeNodes(RE::NiAVObject* a_object, std::function<CESEAdapter::RE::BSVisitControl(RE::BSFadeNode*)> a_func)
		{
			auto result = CESEAdapter::RE::BSVisitControl::kContinue;

			if (!a_object) {
				return result;
			}

			auto fadeNode = Util::Adapter::AsFadeNode(a_object);
			if (fadeNode) {
				result = a_func(fadeNode);

				if (result == CESEAdapter::RE::BSVisitControl::kStop) {
					return result;
				}
			}

			auto node = Util::Adapter::AsNode(a_object);
			if (node) {
				for (auto& child : Util::Adapter::GetChildren(node)) {
					result = ScenegraphFadeNodes(child.get(), a_func);
					if (result == CESEAdapter::RE::BSVisitControl::kStop) {
						break;
					}
				}
			}

			return result;
		}

		// A custom visit controller built to ignore billboard/particle geometry
		static CESEAdapter::RE::BSVisitControl ScenegraphRTGeometries(RE::NiAVObject* a_object, RE::BSFadeNode* validFadeNode, std::function<CESEAdapter::RE::BSVisitControl(RE::BSGeometry*)> a_func)
		{
			auto result = CESEAdapter::RE::BSVisitControl::kContinue;

			if (!a_object) {
				return result;
			}

			auto geom = Util::Adapter::AsGeometry(a_object);
			if (geom) {
				return a_func(geom);
			}

			// Doodlum sez this is faster
			auto rtti = a_object->GetRTTI();

			static REL::Relocation<const RE::NiRTTI*> billboardRTTI{ NiRTTI(NiBillboardNode) };

			if (rtti == billboardRTTI.get())
				return result;

			// Might break vegetation
			static REL::Relocation<const RE::NiRTTI*> orderedRTTI{ NiRTTI(BSOrderedNode) };
			if (rtti == orderedRTTI.get())
				return result;

			auto node = Util::Adapter::AsNode(a_object);

			if (node) {
				for (auto& child : Util::Adapter::GetChildren(node)) {
					if (!child)
						continue;

					if (validFadeNode) {
						if (auto fadeNode = Util::Adapter::AsFadeNode(child.get()); fadeNode && fadeNode != validFadeNode) {
							continue;
						}
					}

					result = ScenegraphRTGeometries(child.get(), validFadeNode, a_func);
					if (result == CESEAdapter::RE::BSVisitControl::kStop) {
						break;
					}
				}
			}

			return result;
		}

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