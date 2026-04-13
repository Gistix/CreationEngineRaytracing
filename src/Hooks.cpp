#include "Hooks.h"
#include "Renderer.h"
#include "Util.h"

#include "Core/D3D12Texture.h"

namespace Hooks
{
	void TES_AttachModel::thunk(RE::TES* tes, RE::TESObjectREFR* refr, RE::TESObjectCELL* cell, void* queuedTree, bool a5, RE::NiAVObject* a6)
	{
		func(tes, refr, cell, queuedTree, a5, a6);

		Scene::GetSingleton()->AttachModel(refr);
	}

	void Release3DRelatedData::thunk(RE::TESObjectREFR* refr)
	{
		Scene::GetSingleton()->GetSceneGraph()->ReleaseFormInstances(refr, true);

		func(refr);
	}

	void Actor_Set3D::thunk(RE::Actor* a_actor, RE::NiAVObject* a_object, bool a_queue3DTasks)
	{
		if (!a_object)
			Scene::GetSingleton()->GetSceneGraph()->ReleaseFormInstances(a_actor, true);

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

		Scene::GetSingleton()->GetSceneGraph()->ReleaseFormInstances(oThis, true);
	};

	void TESWaterSystem_AddWater::thunk(RE::TESWaterSystem* a_waterSystem, RE::NiAVObject* a_waterObj, RE::TESWaterForm* a_waterType, float a_waterHeight, const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>>* a_multiBoundShape, bool a_noDisplacement, bool a_isProcedural)
	{
		func(a_waterSystem, a_waterObj, a_waterType, a_waterHeight, a_multiBoundShape, a_noDisplacement, a_isProcedural);

		Scene::GetSingleton()->GetSceneGraph()->CreateWaterModel(a_waterType, a_waterObj);
	};

	void TESWaterSystem_RemoveWater::thunk(RE::TESWaterSystem* a_waterSystem, RE::NiAVObject* a_waterObj)
	{
		Scene::GetSingleton()->GetSceneGraph()->ReleaseObjectInstance(a_waterObj, true);

		func(a_waterSystem, a_waterObj);
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
	HRESULT CreateTextureAndSRV::thunk(
		ID3D11Device* a_device,
		D3D11_RESOURCE_DIMENSION a_dimension,
		uint32_t a_width,
		uint32_t a_height,
		uint32_t a_depth,
		uint32_t a_mipLevels,
		uint32_t a_arraySize,
		DXGI_FORMAT a_format,
		bool a_cubemap,
		const D3D11_SUBRESOURCE_DATA* a_data,
		RE::BSGraphics::Texture** a_outTexture
	) {
		bool shareTexture = a_dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D && !a_cubemap;

		if (!shareTexture)
			return func(a_device, a_dimension, a_width, a_height, a_depth, a_mipLevels, a_arraySize, a_format, a_cubemap, a_data, a_outTexture);

		auto& expSettings = Scene::GetSingleton()->m_Settings.ExperimentalSettings;

		bool exclusiveMode = expSettings.TextureMode == TextureMode::Exclusive;
		bool cutOff = expSettings.TextureCutOff != 0;

		if (cutOff) {
			uint32_t cutOffSize = 1 << (expSettings.TextureCutOff + 7);
			cutOff = (cutOffSize * cutOffSize) < (a_width * a_height);
		}

		auto* scrapHeap = RE::MemoryManager::GetSingleton()->GetThreadScrapHeap();

		if (exclusiveMode && !cutOff) {
			auto& stateRuntimeData = RE::BSGraphics::State::GetSingleton()->GetRuntimeData();

			auto* texture = reinterpret_cast<RE::BSGraphics::D3D12Texture*>(scrapHeap->Allocate(sizeof(RE::BSGraphics::D3D12Texture), 8));

			if (!texture)
				return E_OUTOFMEMORY;

			std::memset(texture, 0, sizeof(RE::BSGraphics::D3D12Texture));

			auto defaultTexture = stateRuntimeData.defaultTextureGrey->rendererTexture;

			texture->texture = defaultTexture->texture;
			texture->unk08 = defaultTexture->unk08;
			texture->resourceView = defaultTexture->resourceView;
			texture->unk18 = defaultTexture->unk18;
			texture->unk20 = defaultTexture->unk20;

			defaultTexture->texture->AddRef();
			defaultTexture->resourceView->AddRef();

			// We use this as a flag to indicate this 'Texture' is actually 'D3D12Texture'
			texture->pad24 = 1;

			auto renderer = Renderer::GetSingleton();
			auto device = renderer->GetDevice();
			auto nativeDevice = renderer->GetNativeD3D12Device();

			D3D12_RESOURCE_DESC nativeDesc = {};
			nativeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			nativeDesc.Width = a_width;
			nativeDesc.Height = a_height;
			nativeDesc.DepthOrArraySize = 1;
			nativeDesc.MipLevels = static_cast<UINT16>(a_mipLevels);
			nativeDesc.Format = a_format;
			nativeDesc.SampleDesc.Count = 1;
			nativeDesc.SampleDesc.Quality = 0;
			nativeDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

			HRESULT hr = nativeDevice->CreateCommittedResource(
				&heapProps,
				D3D12_HEAP_FLAG_NONE,
				&nativeDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&texture->d3d12Texture)
			);

			if (FAILED(hr)) {
				if (texture->d3d12Texture)
					texture->d3d12Texture->Release();

				return hr;
			}

			auto formatIt = Renderer::GetFormatMapping().find(a_format);

			if (formatIt == Renderer::GetFormatMapping().end()) {
				if (texture->d3d12Texture)
					texture->d3d12Texture->Release();

				return E_FAIL;
			}

			auto& textureDesc = nvrhi::TextureDesc()
				.setWidth(a_width)
				.setHeight(a_height)
				.setMipLevels(a_mipLevels)
				.setFormat(formatIt->second)
				.setInitialState(nvrhi::ResourceStates::CopyDest)
				.setDebugName("Shared Texture [?]");

			auto textureHandle = Renderer::GetSingleton()->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(texture->d3d12Texture), textureDesc);

			// Upload Texture Data
			{
				std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

				auto commandList = renderer->GetGraphicsCommandList();

				commandList->open();

				commandList->beginTrackingTextureState(textureHandle, nvrhi::AllSubresources, nvrhi::ResourceStates::CopyDest);

				for (uint32_t i = 0; i < a_mipLevels; i++)
				{
					const auto& mipData = a_data[i];
					commandList->writeTexture(textureHandle, 0, i, mipData.pSysMem, mipData.SysMemPitch, mipData.SysMemSlicePitch);
				}

				commandList->setPermanentTextureState(textureHandle, nvrhi::ResourceStates::ShaderResource);

				commandList->commitBarriers();

				commandList->close();

				device->executeCommandList(commandList, nvrhi::CommandQueue::Graphics);

				device->waitForIdle();
			}

			*a_outTexture = texture;

			return hr;
		}
		else {
			auto* texture = reinterpret_cast<RE::BSGraphics::Texture*>(scrapHeap->Allocate(sizeof(RE::BSGraphics::Texture), 8));

			if (!texture)
				return E_OUTOFMEMORY;

			std::memset(texture, 0, sizeof(RE::BSGraphics::Texture));

			auto desc = D3D11_TEXTURE2D_DESC{};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = a_mipLevels;
			desc.ArraySize = a_arraySize;
			desc.Format = a_format;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

			auto result = a_device->CreateTexture2D(&desc, a_data, reinterpret_cast<ID3D11Texture2D**>(&texture->texture));

			if (FAILED(result) || !texture->texture) {
				return result;
			}

			auto srvDesc = D3D11_SHADER_RESOURCE_VIEW_DESC{};
			srvDesc.Format = a_format;
			srvDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = a_mipLevels;

			auto srvResult = a_device->CreateShaderResourceView(texture->texture, &srvDesc, &texture->resourceView);

			if (FAILED(srvResult)) {
				texture->texture->Release();
				return srvResult;
			}

			*a_outTexture = texture;

			return result;
		}
	}

	RE::NiSourceTexture* CreateTextureFromDDS::thunk(RE::BSResource::CompressedArchiveStream* a1, char* path, ID3D11ShaderResourceView* srv, char a4, bool a5)
	{
		auto* scene = Scene::GetSingleton();

		std::lock_guard<std::recursive_mutex> lock(scene->shareTextureMutex);

		scene->shareTexture = true;

		auto* result = func(a1, path, srv, a4, a5);

		scene->shareTexture = false;

		return result;
	}

	void* CreateFlowMapSE::thunk(void* a1, int a2, int a3, void* a4)
	{
		auto* scene = Scene::GetSingleton();

		std::lock_guard<std::recursive_mutex> lock(scene->shareTextureMutex);

		scene->shareTexture = true;

		auto* result = func(a1, a2, a3, a4);

		scene->shareTexture = false;

		return result;
	}

	void* CreateFlowMapAE::thunk(void* a1, int a2, int a3, void* a4, int a5, uint32_t a6, bool a7)
	{
		auto* scene = Scene::GetSingleton();

		std::lock_guard<std::recursive_mutex> lock(scene->shareTextureMutex);

		scene->shareTexture = true;

		auto* result = func(a1, a2, a3, a4, a5, a6, a7);

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

	void CreateDepthStencil_Main::thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties)
	{
		auto* scene = Scene::GetSingleton();

		std::lock_guard<std::recursive_mutex> lock(scene->shareTextureMutex);

		scene->shareTexture = true;

		func(This, a_target, a_properties);

		scene->shareTexture = false;
	}

	void CreateRenderTarget_MotionVectors::thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
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
		if (!pass->shader) {
			func(pass, technique, alphaTest, renderFlags);
			return;
		}

		auto shaderType = pass->shader->shaderType.get();

		auto* scene = Scene::GetSingleton();

		if (scene->IsPathTracingActive() && scene->m_Settings.AdvancedSettings.EnableWater) {
			if (shaderType == RE::BSShader::Type::Water)
				return;
		}

		// Cull non-effect models with kRefraction when Path Tracing is active
		if (scene->IsPathTracingActive() && shaderType != RE::BSShader::Type::Effect && pass->shaderProperty) {
			if (pass->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kRefraction))
				return;
		}

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

	int AttachLOD::thunk(RE::BGSObjectLODAttachState* a_state, void* a_arg2, uint32_t a_arg3)
	{
		if (!a_state->isAttached)
			Scene::GetSingleton()->GetSceneGraph()->CreateLODModel(a_state->objectNode);

		return func(a_state, a_arg2, a_arg3);
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
		stl::detour_thunk<CreateTextureAndSRV>(REL::RelocationID(75724, 77538));

		stl::detour_thunk<TES_AttachModel>(REL::RelocationID(13209, 13355));
		stl::detour_thunk<Actor_Set3D>(REL::RelocationID(36199, 37178));

		// Destructor to remove instances (not models)
		stl::detour_thunk<Destructor<RE::NiAVObject>>(REL::RelocationID(68924, 70275));

		stl::detour_thunk<AttachLOD>(REL::RelocationID(30741, 31581));
		
		// Landscape
		stl::detour_thunk<TESObjectLAND_Attach3D>(REL::RelocationID(18334, 18750));
		stl::detour_thunk<TESObjectLAND_Detach3D>(REL::RelocationID(18335, 18751));

		// Water
		stl::detour_thunk<TESWaterSystem_AddWater>(REL::RelocationID(31388, 32179));
		stl::detour_thunk<TESWaterSystem_RemoveWater>(REL::RelocationID(31391, 32182));

		//stl::detour_thunk<CreateTextureFromDDS>(REL::RelocationID(69334, 70716));

		auto createFlowMapRel = REL::RelocationID(31234, 32031).address() + REL::Relocate(0x7E, 0xF8);
		if (REL::Module::IsSE())
			stl::write_thunk_call<CreateFlowMapSE>(createFlowMapRel);
		else
			stl::write_thunk_call<CreateFlowMapAE>(createFlowMapRel);

		stl::write_thunk_call<CreateRenderTarget_PlayerFaceGenTint>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x606, 0x605));

		stl::write_thunk_call<CreateDepthStencil_Main>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x951, 0x951));

		stl::write_thunk_call<CreateRenderTarget_MotionVectors>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x4F0, 0x4EF, 0x64E));

		stl::write_vfunc<0x18, BSCullingProcess_AppendVirtual>(RE::VTABLE_BSCullingProcess[0]);
		stl::write_vfunc<0x18, BSFadeNodeCuller_AppendVirtual>(RE::VTABLE_BSFadeNodeCuller[0]);
		stl::write_vfunc<0x18, NiCullingProcess_AppendVirtual>(RE::VTABLE_NiCullingProcess[0]);

		stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

		auto* scene = Scene::GetSingleton();
		scene->g_FlowMapSize = reinterpret_cast<int32_t*>(REL::RelocationID(527644, 414596).address());
		scene->g_FlowMapSourceTex = reinterpret_cast<RE::NiPointer<RE::NiSourceTexture>*>(REL::RelocationID(527694, 414616).address());
		scene->g_DisplacementCellTexCoordOffset = reinterpret_cast<float4*>(REL::RelocationID(528184, 415129).address());
		scene->g_DisplacementMeshFlowCellOffset = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(528164, 415109).address());

#elif defined(FALLOUT4)
#	if defined(FALLOUT_POST_NG)
		stl::detour_thunk<TES_AttachModel>(REL::ID(2192085));
#	endif
#endif
		logger::info("[Raytracing] Installed hooks");
	}

	void InstallD3D11Hooks([[maybe_unused]]ID3D11Device* device)
	{
		//stl::detour_vfunc<5, ID3D11Device_CreateTexture2D>(device);

		logger::info("[Raytracing] Installed D3D11 hooks");
	}
}
