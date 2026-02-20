#pragma once

namespace Hooks
{
#if defined(SKYRIM)
	struct TES_AttachModel
	{
		static void thunk(RE::TES* a1, RE::TESObjectREFR* refr, RE::TESObjectCELL* cell, void* queuedTree, char a5, RE::NiNode* a6);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderPlayerView
	{
		static void thunk(void* a1, bool a2, bool a3);
		static inline REL::Relocation<decltype(thunk)> func;
	};
#elif defined(FALLOUT4)

#endif

	void Install();
	void InstallEarlyHooks();
	void InstallD3D11Hooks(ID3D11Device* device);
}
