#pragma once

#include "PCH.h"
#include "Types/RE/RE.h"
#include "Types/GeometryRuntimeData.h"
#include "Types/DynamicTrishapeRuntimeData.h"
#include "Types/LightRuntimeData.h"
#include "Types/PointLightRuntimeData.h"
#include "Interop/BoneTransform.hlsli"

namespace Util
{
	namespace Adapter
	{
		namespace Material
		{
#if defined(SKYRIM)
			enum class Feature
			{
				kNone = static_cast<std::underlying_type_t<Feature>>(-1),
				kDefault = 0,
				kEnvironmentMap = 1,
				kGlowMap = 2,
				kParallax = 3,
				kFaceGen = 4,
				kSkinTint = 5,
				kHairTint = 6,
				kParallaxOcc = 7,
				kMultiTexLand = 8,
				kLODLand = 9,
				kUnknown = 10,
				kMultilayerParallax = 11,
				kTreeAnim = 12,
				kMultiIndexTriShapeSnow = 14,
				kLODObjectsHD = 15,
				kEye = 16,
				kCloud = 17,
				kLODLandNoise = 18,
				kMultiTexLandLODBlend = 19
			};
#elif defined(FALLOUT4)
			enum class Feature
			{
				kNone = static_cast<std::underlying_type_t<Feature>>(-1),
				kDefault = 0x0,
				kEnvironmentMap = 0x1, // kEnvmap
				kGlowMap = 0x2, // kGlowmap
				kParallax = 0x3,
				kFaceGen = 0x4,
				kSkinTint = 0x5, // kSkinTint
				kHairTint = 0x6,
				kParallaxOcc = 0x7,
				kMultiTexLand = 0x8, // kLandscape
				kLODLand = 0x9, // kLODLandscape
				kSnow = 0xA,
				kMultiLayerParallax = 0xB,
				kTreeAnim = 0xC,
				kLODObjects = 0xD,
				kMultiIndexTriShapeSnow = 0xE, // kMultiIndexSnow
				kLODObjectsHD = 0xF,
				kEye = 0x10,
				kCloud = 0x11,
				kLODLandNoise = 0x12, // kLODLandscapeNoise
				kMultiTexLandLODBlend = 0x13, // kLODLandscapeBlend
				kDismemberment = 0x14
			};
#endif
		}

		GeometryRuntimeData GetGeometryRuntimeData(RE::BSGeometry* a_geometry);
		DynamicTrishapeRuntimeData GetDynamicTrishapeRuntimeData(RE::BSDynamicTriShape* a_triShape);
		LightRuntimeData GetLightRuntimeData(RE::NiLight* a_light);
		PointLightRuntimeData GetPointLightRuntimeData(RE::NiLight* a_light);

		const char* GetName(RE::TESForm* a_form);

		RE::BIPOBJECT* GetBipedObjects(RE::BipedAnim* a_bipedAnim);
		RE::TESForm* GetBipedObjectItem(const RE::BIPOBJECT& a_bipObject);
		RE::TESBoundObject* GetBaseObject(RE::TESObjectREFR* a_refr);

		RE::BSGeometry* AsGeometry(RE::NiAVObject* a_object);
		RE::BSTriShape* AsTriShape(RE::NiAVObject* a_object);
		RE::NiNode* AsNode(RE::NiAVObject* a_object);
		RE::BSFadeNode* AsFadeNode(RE::NiAVObject* a_object);
		RE::BSSubIndexTriShape* AsSubIndexTriShape(RE::BSGeometry* a_geometry);
		RE::NiSwitchNode* AsSwitchNode(RE::NiAVObject* a_object);

		// This version mimics direct pointer retrieval rather than CommonLib's implementation, which iterates up the parent hierarchy to find a valid owner
		RE::TESObjectREFR* GetOwner(RE::NiAVObject* a_object);

		RE::NiTObjectArray<RE::NiPointer<RE::NiAVObject>>& GetChildren(RE::NiNode* a_node);

		uint8_t* GetVertexData(RE::BSGraphics::TriShape* rendererData);
		uint16_t* GetIndexData(RE::BSGraphics::TriShape* rendererData);
		std::uint32_t GetTriShapeRefCount(RE::BSGraphics::TriShape* triShape);

#if defined(SKYRIM)
		RE::NiSkinInstance* GetSkinInstance(RE::BSGeometry* geometry);
#elif defined(FALLOUT4)
		RE::BSSkin::Instance* GetSkinInstance(RE::BSGeometry* geometry);
#endif

		RE::TESObjectREFR* AsReference(RE::TESForm* a_object);
		RE::ExtraDataList* GetExtraDataList(RE::TESObjectREFR* a_refr);

		// NiAlphaProperty adapters (FO4 lacks getters)
		bool GetAlphaBlending(const RE::NiAlphaProperty* prop);
		bool GetAlphaTesting(const RE::NiAlphaProperty* prop);
		RE::NiAlphaProperty::AlphaFunction GetDestBlendMode(const RE::NiAlphaProperty* prop);
		std::uint8_t GetAlphaTestRef(const RE::NiAlphaProperty* prop);

		// Skin instance adapters
		std::uint32_t GetSkinBoneCount(const void* skinInstance);
		std::uint32_t GetSkinFrameID(const void* skinInstance);
		void GetBoneWorlds(const void* skinInstance, eastl::vector<NiTransformPacked>& outBoneWorlds);
		void GetSkinToBones(const void* skinInstance, eastl::vector<NiTransformPacked>& outSkinToBones);

		// Transform packing helper (used by adapters and SkinnedMesh)
		NiTransformPacked PackTransform(const RE::NiTransform& src);

		// BSTriShape adapter (triangleCount/vertexCount access diverges between SKYRIM/FO4)
		std::uint32_t GetTriangleCount(RE::BSTriShape* triShape);
		std::uint16_t GetVertexCount(RE::BSTriShape* triShape);

		// BSGraphics::State adapters
		std::uint32_t GetFrameCount();
		ID3D11Texture2D* GetProjNoiseTexture();

		// INI Setting adapter
		bool GetINISettingBool(const char* a_name);

		// Portal graph adapter (FO4 has different ShadowSceneNode layout)
		RE::BSPortalGraph* GetPortalGraph(RE::ShadowSceneNode* ssn);

		RE::BSShaderManager::State& GetShaderManagerState();

		bool IsExteriorCell(RE::TESObjectCELL* a_cell);

		RE::EXTERIOR_DATA* GetCellExteriorData(RE::TESObjectCELL* a_cell);

		float GetBoundRadius(const RE::NiBound& bound);

		inline const RE::NiPoint3& GetNiPoint3Zero()
		{
#if defined(SKYRIM)
			return RE::NiPoint3::Zero();
#elif defined(FALLOUT4)
			static const RE::NiPoint3 zero{ 0.0f, 0.0f, 0.0f };
			return zero;
#endif
		}

		ID3D11Texture2D* GetMainDepthStencilTexture();

		float2 GetDynamicResolutionRatios();

		const RE::BSGraphics::ViewData GetCameraEyeViewData();

		bool IsNiAVObjectHidden(const RE::NiAVObject* a_object);

		bool IsMultiBoundNodeAllFail(const RE::BSMultiBoundNode* a_node);

		bool IsMenuOpen(const RE::UI* ui, const RE::BSFixedString& name);
	}
}