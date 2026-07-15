#pragma once

#include "Scene.h"
#include "Types/RE/RE.h"

namespace Hooks
{
#if defined(SKYRIM)
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
#endif

	struct NiSourceTexture_Destructor
	{
		static void thunk(RE::NiSourceTexture* oThis);
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

	struct BSBatchRenderer_RenderPassImmediately
	{
		static void thunk(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_BuildSceneLists
	{
		static void* thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

#elif defined(FALLOUT4)

#endif

	void Install();
	void InstallD3D11(ID3D11Device* a_device);
}
