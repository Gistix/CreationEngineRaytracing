#include "Hooks.h"
#include "Renderer.h"
#include "Util.h"

namespace Hooks
{
	void TES_AttachModel::thunk(RE::TES* tes, RE::TESObjectREFR* refr, RE::TESObjectCELL* cell, void* queuedTree, bool a5, RE::NiAVObject* a6)
	{
		func(tes, refr, cell, queuedTree, a5, a6);

		Scene::GetSingleton()->AttachModel(refr);
	}

	void Release3DRelatedData::thunk(RE::TESObjectREFR* refr)
	{
		Scene::GetSingleton()->GetSceneGraph()->RemoveInstance(refr, true);

		func(refr);
	}


	void Actor_Set3D::thunk(RE::Actor* a_actor, RE::NiAVObject* a_object, bool a_queue3DTasks)
	{
		if (!a_object)
			Scene::GetSingleton()->GetSceneGraph()->RemoveInstance(a_actor, true);

		func(a_actor, a_object, a_queue3DTasks);
	}

	void TESObjectLAND_Attach3D::thunk(RE::TESObjectLAND* oThis, bool a2)
	{
		func(oThis, a2);

		Scene::GetSingleton()->AttachLand(oThis);
	};

	void TESObjectLAND_Detach3D::thunk(RE::TESObjectLAND* oThis)
	{
		func(oThis);

		Scene::GetSingleton()->GetSceneGraph()->RemoveInstance(oThis, true);
	};


	void NiSourceTexture_Destructor::thunk(RE::NiSourceTexture* oThis)
	{
		if (oThis && oThis->rendererTexture) {
			if (auto resource = oThis->rendererTexture->texture) {
				Scene::GetSingleton()->GetSceneGraph()->ReleaseTexture(reinterpret_cast<ID3D11Texture2D*>(resource));
			}
		}

		func(oThis);
	}

	void ShadowSceneNode_AttachObject::thunk(RE::ShadowSceneNode* a_shadowSceneNode, RE::NiAVObject* a_object)
	{
		logger::info("ShadowSceneNode::AttachObject - 0x{:08X} {}", reinterpret_cast<uintptr_t>(a_object), a_object->name);

		func(a_shadowSceneNode, a_object);
	}

	void ShadowSceneNode_DetachObject::thunk(RE::ShadowSceneNode* a_shadowSceneNode, RE::NiAVObject* a_object, bool a3, bool a4)
	{
		logger::info("ShadowSceneNode::DetachObject - 0x{:08X} {}, {}, {}", reinterpret_cast<uintptr_t>(a_object), a_object->name, a3, a4);

		func(a_shadowSceneNode, a_object, a3, a4);
	}

	void TESObjectCELL_AddRefr::thunk(RE::TESObjectCELL* a_cell, RE::TESObjectREFR* a_refr, RE::NiNode* a_node)
	{
		logger::info("TESObjectCELL::AddRefr - 0x{:08X} {} {}", a_refr->formID, a_refr->GetName(), a_node ? a_node->name.c_str() : "N/A" );

		func(a_cell, a_refr, a_node);
	}

#if defined(SKYRIM)
	RE::NiSourceTexture* CreateTextureFromDDS::thunk(RE::BSResource::CompressedArchiveStream* a1, char* path, ID3D11ShaderResourceView* srv, char a4, bool a5)
	{
		auto* scene = Scene::GetSingleton();

		std::lock_guard<std::recursive_mutex> lock(scene->shareTextureMutex);

		scene->shareTexture = true;

		auto* result = func(a1, path, srv, a4, a5);

		scene->shareTexture = false;

		return result;
	}

	void CreateRenderTarget_PlayerFaceGenTint::thunk(RE::BSGraphics::Renderer* oThis, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
	{
		auto* scene = Scene::GetSingleton();

		std::lock_guard<std::recursive_mutex> lock(scene->shareTextureMutex);

		scene->shareTexture = true;

		func(oThis, a_target, a_properties);

		scene->shareTexture = false;
	}

	void CreateDepthStencil_Main::thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties)
	{
		auto* scene = Scene::GetSingleton();

		std::lock_guard<std::recursive_mutex> lock(scene->shareTextureMutex);

		scene->shareTexture = true;

		func(This, a_target, a_properties);

		scene->shareTexture = false;
	}

	void BSCullingProcess_AppendVirtual::thunk(RE::BSCullingProcess* cullingProcess, RE::BSGeometry& geometry, uint32_t a_arg2)
	{
		if (Scene::GetSingleton()->ApplyPathTracingCull())
			return;

		func(cullingProcess, geometry, a_arg2);
	}

	void BSFadeNodeCuller_AppendVirtual::thunk(RE::BSFadeNodeCuller* culler, RE::BSGeometry& geometry, uint32_t a_arg2)
	{
		if (Scene::GetSingleton()->ApplyPathTracingCull())
			return;

		func(culler, geometry, a_arg2);
	}

	void NiCullingProcess_AppendVirtual::thunk(RE::NiCullingProcess* cullingProcess, RE::BSGeometry& geometry, uint32_t a_arg2)
	{
		if (Scene::GetSingleton()->ApplyPathTracingCull())
			return;

		func(cullingProcess, geometry, a_arg2);
	}

	void BSBatchRenderer_RenderPassImmediately::thunk(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags)
	{
		// Skip rendering geometry that has been determined to be occluded
		// Never cull during reflection rendering - reflections need all visible geometry
		if (Scene::GetSingleton()->ApplyPathTracingCull() && pass->shader && pass->geometry) {
			switch (pass->shader->shaderType.get()) {
			case RE::BSShader::Type::Grass:
			case RE::BSShader::Type::Sky:
			case RE::BSShader::Type::Water:
				break;  // Never cull: batched/infinite/reflections
			case RE::BSShader::Type::Utility:
				return;
				break;
			case RE::BSShader::Type::Particle:
			case RE::BSShader::Type::Effect:
				//return;
				break;
			default:  // Lighting, DistantTree, BloodSplatter
				return;
				break;
			}
		}

		func(pass, technique, alphaTest, renderFlags);
	}
#elif defined(FALLOUT4)

#endif

	HRESULT WINAPI ID3D11Device_CreateTexture2D::thunk(ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
	{
		if (!pDesc)
			return func(This, pDesc, pInitialData, ppTexture2D);

		auto* scene = Scene::GetSingleton();

		std::lock_guard<std::recursive_mutex> lock(scene->shareTextureMutex);

		D3D11_TEXTURE2D_DESC descCopy = *pDesc;

		if (scene->shareTexture && !(pDesc->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE))
			descCopy.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;

		return func(This, &descCopy, pInitialData, ppTexture2D);
	}

	void Install()
	{
		stl::write_vfunc<0x6B, Release3DRelatedData>(RE::VTABLE_TESObjectREFR[0]);

		stl::write_vfunc<0x0, NiSourceTexture_Destructor>(RE::VTABLE_NiSourceTexture[0]);

		//stl::detour_thunk<ShadowSceneNode_AttachObject>(REL::RelocationID(99696, 106330));
		//stl::detour_thunk<ShadowSceneNode_DetachObject>(REL::RelocationID(99705, 106339));

		//stl::detour_thunk<TESObjectCELL_AddRefr>(REL::RelocationID(19003, 19411));
		
#if defined(SKYRIM)
		stl::detour_thunk<TES_AttachModel>(REL::RelocationID(13209, 13355));
		stl::detour_thunk<Actor_Set3D>(REL::RelocationID(36199, 37178));
		stl::detour_thunk<TESObjectLAND_Attach3D>(REL::RelocationID(18334, 18750));

		//stl::detour_thunk<TESObjectLAND_Detach3D>(REL::RelocationID(18335, 18751));

		// Destructor to remove instances (not models)
		stl::detour_thunk<Destructor<RE::NiAVObject>>(REL::RelocationID(68924, 70275));

		stl::detour_thunk<CreateTextureFromDDS>(REL::RelocationID(69334, 70716));

		stl::write_thunk_call<CreateRenderTarget_PlayerFaceGenTint>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x606, 0x605, 0x0));

		stl::write_thunk_call<CreateDepthStencil_Main>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x951, 0x951, 0x0));

		stl::write_vfunc<0x18, BSCullingProcess_AppendVirtual>(RE::VTABLE_BSCullingProcess[0]);
		stl::write_vfunc<0x18, BSFadeNodeCuller_AppendVirtual>(RE::VTABLE_BSFadeNodeCuller[0]);
		stl::write_vfunc<0x18, NiCullingProcess_AppendVirtual>(RE::VTABLE_NiCullingProcess[0]);

		stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));;
#elif defined(FALLOUT4)
#	if defined(FALLOUT_POST_NG)
		stl::detour_thunk<TES_AttachModel>(REL::ID(2192085));
#	endif
#endif
		logger::info("[Raytracing] Installed hooks");
	}

	void InstallD3D11Hooks(ID3D11Device* device)
	{
		stl::detour_vfunc<5, ID3D11Device_CreateTexture2D>(device);

		logger::info("[Raytracing] Installed D3D11 hooks");
	}
}
