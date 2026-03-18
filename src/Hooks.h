#pragma once

#include "Scene.h"

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
			Scene::GetSingleton()->GetSceneGraph()->RemoveInstance(oThis);

			func(oThis);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

#if defined(SKYRIM)
	struct CreateTextureFromDDS
	{
		static RE::NiSourceTexture* thunk(RE::BSResource::CompressedArchiveStream* a1, char* path, ID3D11ShaderResourceView* srv, char a4, bool a5);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_PlayerFaceGenTint
	{
		static void thunk(RE::BSGraphics::Renderer* oThis, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateDepthStencil_Main
	{
		static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties);
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
