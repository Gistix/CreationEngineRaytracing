#pragma once

#include "PCH.h"
#include "Types/RE/RE.h"
#include "Types/GeometryRuntimeData.h"
#include "Types/LightRuntimeData.h"
#include "Types/PointLightRuntimeData.h"
#include "Types/CameraRuntimeData.h"
#include "Types/MenuState.h"

namespace RE
{
	namespace BSGraphics
	{
	#if defined(FALLOUT4)
		class TriShape;
	#else
		struct TriShape;
	#endif
		class Texture;
	}

	class BSTriShape;
	class BSDynamicTriShape;
	class BSMultiStreamInstanceTriShape;
	class TESObjectLIGH;
}

class DynamicMesh;

namespace Util
{
	namespace Adapter
	{
		GeometryRuntimeData GetGeometryRuntimeData(RE::BSGeometry* a_geometry);
		LightRuntimeData GetLightRuntimeData(RE::NiLight* a_light);
		PointLightRuntimeData GetPointLightRuntimeData(RE::NiLight* a_light);

		const char* GetName(RE::TESForm* a_form);

		RE::BIPOBJECT* GetBipedObjects(RE::BipedAnim* a_bipedAnim);
		RE::TESForm* GetBipedObjectItem(const RE::BIPOBJECT& a_bipObject);
		RE::TESBoundObject* GetBaseObject(RE::TESObjectREFR* a_refr);

		inline RE::BSGeometry* AsGeometry(RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return a_object->AsGeometry();
#elif defined(FALLOUT4)
			return a_object->IsGeometry();
#endif
		}

		inline RE::BSTriShape* AsTriShape(RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return a_object->AsTriShape();
#elif defined(FALLOUT4)
			return a_object->IsTriShape();
#endif
		}

		inline RE::NiNode* AsNode(RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return a_object->AsNode();
#elif defined(FALLOUT4)
			return a_object->IsNode();
#endif
		}

		inline RE::BSFadeNode* AsFadeNode(RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return a_object->AsFadeNode();
#elif defined(FALLOUT4)
			return a_object->IsFadeNode();
#endif
		}

		inline RE::BSSubIndexTriShape* AsSubIndexTriShape(RE::BSGeometry* a_geometry)
		{
#if defined(SKYRIM)
			return a_geometry->AsSubIndexTriShape();
#elif defined(FALLOUT4)
			return a_geometry->IsSubIndexTriShape();
#endif
		}

		RE::BSMultiStreamInstanceTriShape* AsMultiStreamInstanceTriShape(RE::BSGeometry* a_geometry);

		inline RE::BSDynamicTriShape* AsDynamicTriShape(RE::BSTriShape* a_geometry)
		{
#if defined(SKYRIM)
			return a_geometry->AsDynamicTriShape();
#elif defined(FALLOUT4)
			return a_geometry->IsDynamicTriShape();
#endif
		}

		// This version mimics direct pointer retrieval rather than CommonLib's implementation, which iterates up the parent hierarchy to find a valid owner
		inline RE::TESObjectREFR* GetOwner(RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return a_object->userData;
#elif defined(FALLOUT4)
			return reinterpret_cast<RE::TESObjectREFR*>(a_object->userData);
#endif
		}

		inline RE::BSShaderProperty* GetShaderProperty(const RE::BSGeometry* a_geometry)
		{
#if defined(SKYRIM)
			return a_geometry->shaderProperty.get();
#elif defined(FALLOUT4)
			return reinterpret_cast<RE::BSShaderProperty*>(a_geometry->properties[1].get());
#endif
		}

		inline RE::NiAlphaProperty* GetAlphaProperty(const RE::BSGeometry* a_geometry)
		{
#if defined(SKYRIM)
			return a_geometry->alphaProperty.get();
#elif defined(FALLOUT4)
			return reinterpret_cast<RE::NiAlphaProperty*>(a_geometry->properties[0].get());
#endif
		}

		// Returns the first-person skeleton root (RE::PlayerCharacter::firstPerson3D), or nullptr
		RE::NiNode* GetFirstPerson3D(RE::PlayerCharacter* a_player);
		RE::NiPoint3 GetZeroNiPoint3();
		RE::NiTexture* GetDefaultTextureProjNoiseMap();
		ID3D11Texture2D* GetTextureResource(RE::NiTexture* a_texture);

		// Returns the first-person node position (eye position) via PlayerCamera::GetFirstPersonNodePosition (Skyrim only)
		RE::NiPoint3 GetCameraEyePosition();
		bool IsInFirstPerson(RE::PlayerCharacter* a_player, RE::PlayerCamera* a_camera);
		RE::NiPoint3 GetFirstPersonNodePosition(RE::PlayerCamera* a_camera);

		inline RE::NiTObjectArray<RE::NiPointer<RE::NiAVObject>>& GetChildren(RE::NiNode* a_node)
		{
#if defined(SKYRIM)
			return a_node->children;
#elif defined(FALLOUT4)
			return a_node->children;
#endif
		}

		inline RE::NiAVObject* GetChildAt(RE::NiNode* a_node, uint16_t a_index)
		{
#if defined(SKYRIM)
			auto& children = a_node->children;
			if (a_index < children.size()) {
				return children[a_index].get();
			}
#elif defined(FALLOUT4)
			auto& children = a_node->children;
			if (a_index < children.size()) {
				return children.data()[a_index].get();
			}
#endif
			return nullptr;
		}

		uint8_t* GetVertexData(RE::BSGraphics::TriShape* rendererData);
		uint16_t* GetIndexData(RE::BSGraphics::TriShape* rendererData);
		void DeallocateTriShapeData(RE::BSGraphics::TriShape* rendererData);

		ID3D12Resource* GetVertexBufferDX12(RE::BSGraphics::TriShape* a_triShape);
		ID3D12Resource* GetIndexBufferDX12(RE::BSGraphics::TriShape* a_triShape);
		ID3D11Buffer* GetD3D11VertexBuffer(RE::BSGraphics::TriShape* a_triShape);
		ID3D11Buffer* GetD3D11IndexBuffer(RE::BSGraphics::TriShape* a_triShape);

		struct SkinData
		{
			bool hasSkin;
			uint32_t numBones;
			const RE::NiTransform** boneWorldTransforms;
			uint32_t frameID;
		};

		SkinData GetSkinData(RE::BSGeometry* geometry);
		const RE::NiTransform* GetSkinToBoneTransform(RE::BSGeometry* geometry, uint32_t a_boneIndex);

		struct ShaderPropertyRuntimeData
		{
			uint64_t flags = 0;
			float alpha = 1.0f;
			uint32_t materialType = 0;
			RE::BSShaderMaterial* material = nullptr;
			float3 emissiveColor{};
			float emissiveScale = 0.0f;
			float4 projectedUVParams{};
			float4 projectedUVColor{};
		};

		RE::BSGraphics::Texture* GetRendererTexture(RE::NiTexture* a_texture);

		RE::BSMultiBound* GetMultiBound(RE::BSMultiBoundNode* a_node);
		RE::BSMultiBoundAABB* GetMultiBoundAABB(RE::BSMultiBound* a_multiBound);

		float GetNiBoundRadius(const RE::NiBound& a_bound);

		struct TrishapeRuntimeData
		{
			uint32_t vertexCount;
			uint32_t triangleCount;
		};

		float4 GetShaderManagerLoadedRange();

		bool IsSkinned(const GeometryRuntimeData& geometryData);
		TrishapeRuntimeData GetTrishapeRuntimeData(RE::BSTriShape* a_triShape);
		RE::BSGraphics::TriShape* GetRendererData(const GeometryRuntimeData& geometryData);
		bool GetAlphaBlending(RE::NiAlphaProperty* a_property);
		
		uint32_t GetDynamicDataSize(RE::BSDynamicTriShape* a_dynamicTriShape);
		void* LockDynamicData(RE::BSDynamicTriShape* a_dynamicTriShape);
		void UnlockDynamicData(RE::BSDynamicTriShape* a_dynamicTriShape);

		void UpdateDynamicData(DynamicMesh* dynamicMesh, RE::BSDynamicTriShape* bsDynamicTriShape);
		void GetAlwaysRenderChildren(RE::NiNode* shadowSceneNode, eastl::vector<RE::NiAVObject*>& outChildren);
		bool IsValidTriShape(RE::BSGeometry* a_geometry, bool allowInstancedTriShape);

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

		ID3D11Texture2D* GetMainDepthStencilTexture();

		float2 GetDynamicResolutionRatios();

		const RE::BSGraphics::ViewData GetCameraEyeViewData();
		CameraRuntimeData GetCameraRuntimeData();

		RE::SceneGraph* GetWorldRootNode();

		inline bool IsNiAVObjectHidden(const RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return a_object->flags.all(RE::NiAVObject::Flag::kHidden);
#elif defined(FALLOUT4)
			return (a_object->GetFlags() & 1) != 0;
#endif
		}

		bool IsMultiBoundNodeAllFail(const RE::BSMultiBoundNode* a_node);

		RE::BSGraphics::State& GetGraphicsState();
		uint32_t GetGraphicsFrameCount();

		CESEAdapter::REX::EnumSet<MenuState> GetMenuState();

		RE::NiSwitchNode* AsSwitchNode(RE::NiNode* node);

		RE::BSPortalGraph* GetPortalGraph(RE::NiNode* node);
		RE::ShadowSceneNode* GetShadowSceneNode(uint32_t index = 0);

		RE::NiIntegersExtraData* GetIntegersExtraData(RE::BSTriShape* a_triShape, const char* a_name);
		RE::NiBinaryExtraData* GetBinaryExtraData(RE::BSTriShape* a_triShape, const char* a_name);

		RE::TESObjectREFR* GetUserData(RE::NiAVObject* object);
		bool IsSpotLight(const RE::TESObjectLIGH* a_light);
		RE::BSMultiBoundNode* AsMultiBoundNode(RE::NiNode* a_node);
	}
}
