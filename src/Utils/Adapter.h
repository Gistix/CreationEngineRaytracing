#pragma once

#include "PCH.h"
#include "Types/RE/RE.h"
#include "Types/GeometryRuntimeData.h"
#include "Types/LightRuntimeData.h"
#include "Types/PointLightRuntimeData.h"

namespace Util
{
	namespace Adapter
	{
		GeometryRuntimeData GetGeometryRuntimeData(RE::BSGeometry* a_geometry);
		LightRuntimeData GetLightRuntimeData(RE::NiLight* a_light);
		PointLightRuntimeData GetPointLightRuntimeData(RE::NiLight* a_light);

		RE::BIPOBJECT* GetBipedObjects(RE::BipedAnim* a_bipedAnim);
		RE::TESForm* GetBipedObjectItem(RE::BIPOBJECT* a_bipObject);
		RE::TESBoundObject* GetBaseObject(RE::TESObjectREFR* a_refr);

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

		RE::BSShaderManager::State& GetShaderManagerState();

		bool IsExteriorCell(RE::TESObjectCELL* a_cell);

		RE::EXTERIOR_DATA* GetCellExteriorData(RE::TESObjectCELL* a_cell);

		inline const RE::NiPoint3& GetNiPoint3Zero()
		{
#if defined(SKYRIM)
			return RE::NiPoint3::Zero();
#elif defined(FALLOUT4)
			static const RE::NiPoint3 zero{ 0.0f, 0.0f, 0.0f };
			return zero;
#endif
		}

		REX::W32::ID3D11Texture2D* GetMainDepthStencilTexture();

		float2 GetDynamicResolutionRatios();

		const RE::BSGraphics::ViewData& GetCameraEyeViewData();

		bool IsNiAVObjectHidden(const RE::NiAVObject* a_object);

		bool IsMultiBoundNodeAllFail(const RE::BSMultiBoundNode* a_node);
	}
}