#include "Hooks.h"
#include "Renderer.h"
#include "Scene.h"

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
		Renderer::GetSingleton()->ExecutePasses();

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
