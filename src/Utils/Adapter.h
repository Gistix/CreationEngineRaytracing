#pragma once

#include "PCH.h"
#include "Types/RE/RE.h"
#include "Types/GeometryRuntimeData.h"

namespace Util
{
	namespace Adapter
	{
		GeometryRuntimeData GetGeometryRuntimeData(RE::BSGeometry* a_geometry);

		RE::BIPOBJECT* GetBipedObjects(RE::BipedAnim* a_bipedAnim);

		RE::BSGeometry* AsGeometry(RE::NiAVObject* a_object);
		RE::NiNode* AsNode(RE::NiAVObject* a_object);
		RE::BSFadeNode* AsFadeNode(RE::NiAVObject* a_object);
		RE::BSSubIndexTriShape* AsSubIndexTriShape(RE::BSGeometry* a_geometry);
		
		RE::NiTObjectArray<RE::NiPointer<RE::NiAVObject>>& GetChildren(RE::NiNode* a_node);

		uint8_t* GetVertexData(RE::BSGraphics::TriShape* rendererData);
		uint16_t* GetIndexData(RE::BSGraphics::TriShape* rendererData);

#if defined(SKYRIM)
		RE::NiSkinInstance* GetSkinInstance(RE::BSGeometry* geometry);
#elif defined(FALLOUT4)
		RE::BSSkin::Instance* GetSkinInstance(RE::BSGeometry* geometry);
#endif

		RE::TESObjectREFR* AsReference(RE::TESForm* a_object);
		RE::ExtraDataList* GetExtraDataList(RE::TESObjectREFR* a_refr);
	}
}