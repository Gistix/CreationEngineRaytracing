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

		template <typename Func>
		static CESEAdapter::RE::BSVisitControl ScenegraphTriShapes(
			RE::NiAVObject* a_object, 
			Func&& a_func,
			RE::TESObjectREFR* parentRefr = nullptr,
			RE::NiAVObject* firstPersonRoot = nullptr)
		{
			auto result = CESEAdapter::RE::BSVisitControl::kContinue;

			if (!a_object)
				return result;

			const bool isVisibleFP = (a_object == firstPersonRoot);
			if (!isVisibleFP && Util::Adapter::IsNiAVObjectHidden(a_object))
				return result;

			// Early return for TriShapes — most common actionable leaf
			if (auto geom = Util::Adapter::AsTriShape(a_object))
				return a_func(geom, parentRefr);

			auto rtti = a_object->GetRTTI();

			if (rtti == Constants::rtti::NiBillboardNode.get())
				return result;

			if (rtti == Constants::rtti::BSOrderedNode.get())
				return result;

			// Only nodes can have children or be FadeNodes
			if (auto node = Util::Adapter::AsNode(a_object))
			{
				auto& children = Util::Adapter::GetChildren(node);
				if (auto switchNode = Util::Adapter::AsSwitchNode(node)) {
					auto index = static_cast<uint16_t>(switchNode->index);
					if (index < children.size())
						result = ScenegraphTriShapes(children[index].get(), a_func, parentRefr, firstPersonRoot);
				}
				else {
					// Propagate owner refr through FadeNodes
					auto refr = parentRefr;
					if (rtti == Constants::rtti::BSFadeNode.get()) {
						if (auto owner = Util::Adapter::GetOwner(a_object))
							refr = owner;
					}
					else if (rtti == Constants::rtti::ShadowSceneNode.get()) {
						if (auto portalGraph = Util::Adapter::GetPortalGraph(node)) {
							// Iterate over PortalGraph always render children
							// This list contains rendered nodes that are outside of the normal SceneGraph
#if defined(SKYRIM)
							auto& renderChildren = portalGraph->alwaysRenderChildren;
#elif defined(FALLOUT4)
							auto& renderChildren = portalGraph->alwayRenderChildren;
#endif
							for (auto& child : renderChildren)
							{
								// Only those who are outside the Scenegraph
								if (child->parent)
									continue;

								result = ScenegraphTriShapes(child.get(), a_func, refr, firstPersonRoot);
								if (result == CESEAdapter::RE::BSVisitControl::kStop)
									break;
							}
						}
					}

					for (auto& child : children) {
						result = ScenegraphTriShapes(child.get(), a_func, refr, firstPersonRoot);
						if (result == CESEAdapter::RE::BSVisitControl::kStop)
							break;
					}
				}
			}

			return result;
		}
	}
}