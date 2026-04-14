#pragma once

#include "Scene.h"
#include "Types/RE/RE.h"

namespace Hooks
{
	struct TES_AttachModel
	{
		static void thunk(RE::TES* a1, RE::TESObjectREFR* refr, RE::TESObjectCELL* cell, void* queuedTree, bool a5, RE::NiAVObject* a6);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Release3DRelatedData
	{
		static void thunk(RE::TESObjectREFR* oThis);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Actor_Set3D
	{
		static void thunk(RE::Actor* oThis, RE::NiAVObject* a_object, bool a_queue3DTasks = true);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESObjectLAND_Attach3D
	{
		static void thunk(RE::TESObjectLAND* oThis, bool a2);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESObjectLAND_Detach3D
	{
		static void thunk(RE::TESObjectLAND* oThis);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESWaterSystem_AddWater
	{
		static void thunk(RE::TESWaterSystem* a_waterSystem, RE::NiAVObject* a_waterObj, RE::TESWaterForm* a_waterType, float a_waterHeight, const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>>* a_multiBoundShape, bool a_noDisplacement, bool a_isProcedural);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESWaterSystem_RemoveWater
	{
		static void thunk(RE::TESWaterSystem* a_waterSystem, RE::NiAVObject* a_waterObj);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct NiSourceTexture_Destructor
	{
		static void thunk(RE::NiSourceTexture* oThis);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <typename T>
	struct Destructor
	{
		static void thunk(T* oThis)
		{
			Scene::GetSingleton()->GetSceneGraph()->ReleaseObjectInstance(oThis);

			func(oThis);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ShadowSceneNode_AttachObject
	{
		static void thunk(RE::ShadowSceneNode* oThis, RE::NiAVObject* a_object);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ShadowSceneNode_DetachObject
	{
		static void thunk(RE::ShadowSceneNode* oThis, RE::NiAVObject* a_object, bool a3, bool a4);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESObjectCELL_AddRefr
	{
		static void thunk(RE::TESObjectCELL* a_cell, RE::TESObjectREFR* a_refr, RE::NiNode* a_node);
		static inline REL::Relocation<decltype(thunk)> func;
	};

#if defined(SKYRIM)
	struct CreateTextureAndSRV
	{
		static HRESULT thunk(
			ID3D11Device* a_device,
			D3D11_RESOURCE_DIMENSION a_dimension,
			uint32_t a_width,
			uint32_t a_height,
			uint32_t a_depth,
			uint32_t a_mipLevels,
			uint32_t a_arraySize,
			DXGI_FORMAT a_format,
			bool a_cubeMap,
			const D3D11_SUBRESOURCE_DATA* a_data,
			RE::BSGraphics::Texture** a_outTexture);

		static inline REL::Relocation<decltype(thunk)> func;
	};

	/*struct CreateTextureAndSRV
	{
		static HRESULT thunk(
			ID3D11Device* a_device,
			int a_textureType, 
			uint32_t a_width, 
			uint32_t a_height, 
			__int64 a_pitchOrLinearSize, 
			uint32_t a_mipCount, 
			uint32_t a_sliceCount, 
			uint8_t a_format,
			bool a_isCubeMap,
			__int64 a_pixelData,
			RE::BSGraphics::Texture** a_outTexture);

		static inline REL::Relocation<decltype(thunk)> func;
	};*/

	struct CreateRenderTarget
	{
		static void thunk(RE::BSGraphics::Renderer* a_renderer, RE::RENDER_TARGETS::RENDER_TARGET a_target, const char* a_RenderTarget, RE::BSGraphics::RenderTargetProperties* a_properties);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateDepthStencil
	{
		static void thunk(RE::BSGraphics::Renderer* a_renderer, RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL a_target, const char* a_depthStencilTarget, RE::BSGraphics::DepthStencilTargetProperties* a_properties);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateFlowMapSE
	{
		static void* thunk(void* a1, int a2, int a3, void* a4);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateFlowMapAE
	{
		static void* thunk(void* a1, int a2, int a3, void* a4, int a5, uint32_t a6, bool a7);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSCullingProcess_AppendVirtual
	{
		static void thunk(RE::BSCullingProcess* cullingProcess, RE::BSGeometry& geometry, uint32_t a_arg2);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSFadeNodeCuller_AppendVirtual
	{
		static void thunk(RE::BSFadeNodeCuller* culler, RE::BSGeometry& geometry, uint32_t a_arg2);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct NiCullingProcess_AppendVirtual
	{
		static void thunk(RE::NiCullingProcess* cullingProcess, RE::BSGeometry& geometry, uint32_t a_arg2);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSBatchRenderer_RenderPassImmediately
	{
		static void thunk(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct AttachLOD
	{
		static int thunk(RE::BGSObjectLODAttachState* a_state, void* a_arg2, uint32_t a_arg3);
		static inline REL::Relocation<decltype(thunk)> func;
	};
#elif defined(FALLOUT4)

#endif

	struct ID3D11Device_CreateTexture2D
	{
		static HRESULT WINAPI thunk(ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void Install();
	void InstallEarlyHooks();
	void InstallD3D11Hooks(ID3D11Device* device);
}
