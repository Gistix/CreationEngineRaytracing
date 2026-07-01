#pragma once

#include "Constants.h"

#include "Core/WorkerPool.h"

#include <functional>

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

		// A custom visit controller built to pass down visibility and owner reference
		static CESEAdapter::RE::BSVisitControl ScenegraphTriShapes(
			RE::NiAVObject* a_object, 
			std::function<CESEAdapter::RE::BSVisitControl(RE::BSTriShape*, bool, RE::TESObjectREFR*)> a_func, 
			bool parentHidden = false, RE::TESObjectREFR* parentRefr = nullptr)
		{
			auto result = CESEAdapter::RE::BSVisitControl::kContinue;

			if (!a_object)
				return result;

			auto rtti = a_object->GetRTTI();

			if (rtti == Constants::rtti::NiBillboardNode.get())
				return result;

			if (rtti == Constants::rtti::BSOrderedNode.get())
				return result;

			bool hidden = parentHidden || Util::Adapter::IsNiAVObjectHidden(a_object);

			// Set as parent refr to propagate it downwards
			auto refr = parentRefr; 

			// Update refr if it actually exists (else keep the parent refr)
			if (auto fadeNode = Util::Adapter::AsFadeNode(a_object))
				if (auto owner = Util::Adapter::GetOwner(fadeNode))
					refr = owner;

			auto geom = Util::Adapter::AsTriShape(a_object);
			if (geom) {
				return a_func(geom, hidden, refr);
			}

			auto node = Util::Adapter::AsNode(a_object);
			if (node) {
				for (auto& child : Util::Adapter::GetChildren(node)) {
					if (!child)
						continue;

					result = ScenegraphTriShapes(child.get(), a_func, hidden, refr);
					if (result == CESEAdapter::RE::BSVisitControl::kStop) {
						break;
					}
				}
			}

			return result;
		}

		// Parallel variant. When a BSFadeNode introduces a non-null owner, the FadeNode
		// subtree is dispatched as a unit to one worker via ownedHandler. Orphans are
		// processed inline via orphanFunc on the calling thread.
		template <typename OrphanFunc, typename OwnedHandler>
		static void ScenegraphTriShapesParallel(
			RE::NiAVObject* a_object,
			OrphanFunc&& orphanFunc,
			OwnedHandler& ownedHandler,
			WorkerPool& workerPool,
			bool parentHidden = false,
			RE::TESObjectREFR* parentRefr = nullptr)
		{
			if (!a_object)
				return;

			auto rtti = a_object->GetRTTI();

			if (rtti == Constants::rtti::NiBillboardNode.get())
				return;

			if (rtti == Constants::rtti::BSOrderedNode.get())
				return;

			bool hidden = parentHidden || Util::Adapter::IsNiAVObjectHidden(a_object);

			auto refr = parentRefr;

			if (auto fadeNode = Util::Adapter::AsFadeNode(a_object))
				if (auto owner = Util::Adapter::GetOwner(fadeNode))
					refr = owner;

			if (refr) {
				workerPool.Enqueue([a_object, hidden, refr, &ownedHandler](nvrhi::ICommandList* cl) {
					ownedHandler(a_object, hidden, refr, cl);
				});
				return;
			}

			auto geom = Util::Adapter::AsTriShape(a_object);
			if (geom) {
				orphanFunc(geom, hidden, refr);
				return;
			}

			auto node = Util::Adapter::AsNode(a_object);
			if (node) {
				for (auto& child : Util::Adapter::GetChildren(node)) {
					if (!child)
						continue;

					ScenegraphTriShapesParallel(child.get(), orphanFunc, ownedHandler, workerPool, hidden, refr);
				}
			}
		}
	}
}