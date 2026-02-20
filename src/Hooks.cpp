#include "Hooks.h"
#include "Renderer.h"
#include "Scene.h"
#include "Util.h"

namespace Hooks
{
#if defined(SKYRIM)
	void TES_AttachModel::thunk(RE::TES* tes, RE::TESObjectREFR* refr, RE::TESObjectCELL* cell, void* queuedTree, char a5, RE::NiNode* a6)
	{
		func(tes, refr, cell, queuedTree, a5, a6);

		Scene::GetSingleton()->AttachModel(refr);
	}

	void Main_RenderPlayerView::thunk(void* a1, bool a2, bool a3)
	{
		auto* renderer = Renderer::GetSingleton();

		auto& runtimeData = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();
		
		auto cameraData = runtimeData.cameraData.getEye();

		float2 ndcToViewMult = float2(2.0f / cameraData.projMat(0, 0), -2.0f / cameraData.projMat(1, 1));
		float2 ndcToViewAdd = float2(-1.0f / cameraData.projMat(0, 0), 1.0f / cameraData.projMat(1, 1));

		renderer->UpdateCameraData(
			cameraData.viewMat.Invert().Transpose(),
			cameraData.projMat.Invert().Transpose(),
			Util::Game::GetClippingData(),
			float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y),
			Util::Float3(runtimeData.posAdjust.getEye())
		);

		renderer->ExecutePasses();

		func(a1, a2, a3);
	}
#elif defined(FALLOUT4)

#endif

	void Install()
	{
#if defined(SKYRIM)
		stl::detour_thunk<TES_AttachModel>(REL::RelocationID(13209, 13355));
		stl::detour_thunk<Main_RenderPlayerView>(REL::RelocationID(35560, 36559));
#elif defined(FALLOUT4)

#endif
		logger::info("[Raytracing] Installed hooks");
	}

	void InstallD3D11Hooks(ID3D11Device* device)
	{
		logger::info("[Raytracing] Installed D3D11 hooks - [0x{:08X}]", reinterpret_cast<uintptr_t>(device));
	}
}
