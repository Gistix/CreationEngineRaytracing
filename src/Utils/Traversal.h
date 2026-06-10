#pragma once

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

			auto fadeNode = Util::Adapter::CLib::AsFadeNode(a_object);
			if (fadeNode) {
				result = a_func(fadeNode);

				if (result == CESEAdapter::RE::BSVisitControl::kStop) {
					return result;
				}
			}

			auto node = Util::Adapter::CLib::AsNode(a_object);
			if (node) {
				for (auto& child : Util::Adapter::CLib::GetChildren(node)) {
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

			auto geom = Util::Adapter::CLib::AsGeometry(a_object);
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

			auto node = Util::Adapter::CLib::AsNode(a_object);

			if (node) {
				for (auto& child : Util::Adapter::CLib::GetChildren(node)) {
					if (!child)
						continue;

					if (validFadeNode) {
						if (auto fadeNode = Util::Adapter::CLib::AsFadeNode(child.get()); fadeNode && fadeNode != validFadeNode) {
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

	}
}