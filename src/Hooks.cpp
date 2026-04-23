#include "Hooks.h"
#include "Renderer.h"
#include "Util.h"

#include "Core/D3D12Texture.h"

namespace Hooks
{
	struct TES_AttachModel
	{
		static void thunk(RE::TES* tes, RE::TESObjectREFR* refr, RE::TESObjectCELL* cell, void* queuedTree, bool a5, RE::NiAVObject* a6)
		{
			func(tes, refr, cell, queuedTree, a5, a6);

			Scene::GetSingleton()->AttachModel(refr);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	void Release3DRelatedData::thunk(RE::TESObjectREFR* refr)
	{
		Scene::GetSingleton()->GetSceneGraph()->ReleaseInstances(refr, true);

		func(refr);
	}

	void Actor_Set3D::thunk(RE::Actor* a_actor, RE::NiAVObject* a_object, bool a_queue3DTasks)
	{
		if (!a_object)
			Scene::GetSingleton()->GetSceneGraph()->ReleaseInstances(a_actor, true);

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

		Scene::GetSingleton()->GetSceneGraph()->ReleaseInstances(oThis, true);
	};

	void TESWaterSystem_AddWater::thunk(RE::TESWaterSystem* a_waterSystem, RE::NiAVObject* a_waterObj, RE::TESWaterForm* a_waterType, float a_waterHeight, const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>>* a_multiBoundShape, bool a_noDisplacement, bool a_isProcedural)
	{
		func(a_waterSystem, a_waterObj, a_waterType, a_waterHeight, a_multiBoundShape, a_noDisplacement, a_isProcedural);

		Scene::GetSingleton()->GetSceneGraph()->CreateWaterModel(a_waterType, a_waterObj);
	};

	void TESWaterSystem_RemoveWater::thunk(RE::TESWaterSystem* a_waterSystem, RE::NiAVObject* a_waterObj)
	{
		Scene::GetSingleton()->GetSceneGraph()->ReleaseWaterInstance(a_waterObj);

		func(a_waterSystem, a_waterObj);
	};

	void NiSourceTexture_Destructor::thunk(RE::NiSourceTexture* oThis)
	{
		if (oThis && oThis->rendererTexture) {
			auto scene = Scene::GetSingleton();
			auto sceneGraph = scene->GetSceneGraph();
			sceneGraph->ReleaseTexture(oThis->rendererTexture);
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

	struct TESObject_UnClone3D
	{
		static void thunk(RE::TESObject* a_object, RE::TESObjectREFR* a_refr)
		{
			logger::trace("TESObject::UnClone3D - Object {:0X} {}", a_object->formID, a_object->GetName());

			if (a_refr) {
				logger::trace("TESObject::UnClone3D - Refr {:0X}", a_refr->formID);

				if (auto* baseObject = a_refr->GetBaseObject()) {
					if (auto* model = baseObject->As<RE::TESModel>())
						logger::trace("\tTESObject::UnClone3D - Model: {}", model->GetModel());
				}

				Scene::GetSingleton()->GetSceneGraph()->ReleaseInstances(a_refr, true);
			}

			func(a_object, a_refr);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

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
			cutOff = (a_width * a_height) < (cutOffSize * cutOffSize);
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

	void CreateRenderTarget::thunk(
		RE::BSGraphics::Renderer* a_renderer, 
		RE::RENDER_TARGETS::RENDER_TARGET a_target, 
		const char* a_RenderTarget, 
		RE::BSGraphics::RenderTargetProperties* a_properties)
	{
		switch (a_target)
		{
		case RE::RENDER_TARGETS::kMOTION_VECTOR:
		case RE::RENDER_TARGETS::kPLAYER_FACEGEN_TINT:
			break;
		default:
			func(a_renderer, a_target, a_RenderTarget, a_properties);
			return;
		}

		auto desc = D3D11_TEXTURE2D_DESC{};
		desc.Width = a_properties->width;
		desc.Height = a_properties->height;
		desc.MipLevels = a_properties->allowMipGeneration ? 0 : 1;
		desc.ArraySize = 1;
		desc.Format = static_cast<DXGI_FORMAT>(a_properties->format.underlying());
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;

		// Yes, we created this entire function just to set the texture as shared
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

		if (a_properties->supportUnorderedAccess)
			desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		if (a_properties->allowMipGeneration)
			desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;

		auto& renderTexture = a_renderer->GetRuntimeData().renderTargets[a_target];

		auto device = reinterpret_cast<ID3D11Device*>(a_renderer->GetDevice());

		device->CreateTexture2D(&desc, nullptr, &renderTexture.texture);
		device->CreateRenderTargetView(renderTexture.texture, nullptr, &renderTexture.RTV);
		device->CreateShaderResourceView(renderTexture.texture, nullptr, &renderTexture.SRV);

		if (a_properties->copyable)
		{
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			device->CreateTexture2D(&desc, nullptr, &renderTexture.textureCopy);
			device->CreateShaderResourceView(renderTexture.textureCopy, nullptr, &renderTexture.SRVCopy);
		}

		if (a_properties->supportUnorderedAccess)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			device->CreateUnorderedAccessView(renderTexture.texture, &uavDesc, &renderTexture.UAV);
		}
	}

	void CreateDepthStencil::thunk(
		RE::BSGraphics::Renderer* a_renderer,
		RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL a_target,
		[[ maybe_unused ]] const char* a_depthStencilTarget,
		RE::BSGraphics::DepthStencilTargetProperties* a_properties)
	{
		DXGI_FORMAT texFormat, dsvFormat, srvFormat;
		bool stencil = a_properties->stencil;

		if (a_properties->use16BitsDepth)
		{
			texFormat = DXGI_FORMAT_R16_TYPELESS;
			dsvFormat = DXGI_FORMAT_D16_UNORM;
			srvFormat = DXGI_FORMAT_R16_UNORM;
		}
		else
		{
			texFormat = DXGI_FORMAT_R32_TYPELESS;
			dsvFormat = DXGI_FORMAT_D32_FLOAT;
			srvFormat = DXGI_FORMAT_R32_FLOAT;
		}

		if (stencil)
		{
			texFormat = DXGI_FORMAT_R24G8_TYPELESS;
			dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		}

		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = a_properties->width;
		texDesc.Height = a_properties->height;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = a_properties->arraySize;
		texDesc.Format = texFormat;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		texDesc.CPUAccessFlags = 0;

		// Yes, we created this entire function just to set the texture as shared (2)
		texDesc.MiscFlags = a_target == RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN ? D3D11_RESOURCE_MISC_SHARED : 0;

		auto& depthStencil = a_renderer->GetDepthStencilData().depthStencils[a_target];
		auto device = reinterpret_cast<ID3D11Device*>(a_renderer->GetDevice());

		device->CreateTexture2D(&texDesc, nullptr, &depthStencil.texture);

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc2 = {};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

		srvDesc.Format = srvFormat;

		uint32_t arraySize = a_properties->arraySize;

		if (arraySize > 1)
		{
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MipLevels = 1;
			srvDesc.Texture2DArray.ArraySize = arraySize;
		}
		else
		{
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
		}

		for (uint32_t i = 0; i < arraySize; ++i)
		{
			if (arraySize > 1)
			{
				dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				dsvDesc.Texture2DArray.MipSlice = 0;
				dsvDesc.Texture2DArray.FirstArraySlice = i;
				dsvDesc.Texture2DArray.ArraySize = 1;

				dsvDesc2.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				dsvDesc2.Texture2DArray.MipSlice = 0;
				dsvDesc2.Texture2DArray.FirstArraySlice = i;
				dsvDesc2.Texture2DArray.ArraySize = 1;
			}
			else
			{
				dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Texture2D.MipSlice = 0;

				dsvDesc2.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
				dsvDesc2.Texture2D.MipSlice = 0;
			}

			dsvDesc.Format = dsvFormat;
			dsvDesc.Flags = 0;

			dsvDesc2.Format = dsvFormat;
			dsvDesc2.Flags = D3D11_DSV_READ_ONLY_DEPTH | (stencil ? D3D11_DSV_READ_ONLY_STENCIL : 0);

			device->CreateDepthStencilView(depthStencil.texture, &dsvDesc, &depthStencil.views[i]);
			device->CreateDepthStencilView(depthStencil.texture, &dsvDesc2, &depthStencil.readOnlyViews[i]);
		}

		device->CreateShaderResourceView(depthStencil.texture, &srvDesc, &depthStencil.depthSRV);

		if (stencil)
		{
			srvDesc.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
			device->CreateShaderResourceView(depthStencil.texture, &srvDesc, &depthStencil.stencilSRV);
		}
	}

	struct CreateFlowMap
	{
		static uint32_t sub_140E4C440(DXGI_FORMAT a_format)
		{
			switch (a_format)
			{
			case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
			case DXGI_FORMAT_R32G32B32A32_UINT:
			case DXGI_FORMAT_R32G32B32A32_SINT:
				return 128;
			case DXGI_FORMAT_R32G32B32_TYPELESS:
			case DXGI_FORMAT_R32G32B32_FLOAT:
			case DXGI_FORMAT_R32G32B32_UINT:
			case DXGI_FORMAT_R32G32B32_SINT:
				return 96;
			case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_UNORM:
			case DXGI_FORMAT_R16G16B16A16_UINT:
			case DXGI_FORMAT_R16G16B16A16_SNORM:
			case DXGI_FORMAT_R16G16B16A16_SINT:
			case DXGI_FORMAT_R32G32_TYPELESS:
			case DXGI_FORMAT_R32G32_FLOAT:
			case DXGI_FORMAT_R32G32_UINT:
			case DXGI_FORMAT_R32G32_SINT:
			case DXGI_FORMAT_R32G8X24_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
			case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
				return 64;
			case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			case DXGI_FORMAT_R10G10B10A2_UINT:
			case DXGI_FORMAT_R11G11B10_FLOAT:
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_R8G8B8A8_UINT:
			case DXGI_FORMAT_R8G8B8A8_SNORM:
			case DXGI_FORMAT_R8G8B8A8_SINT:
			case DXGI_FORMAT_R16G16_TYPELESS:
			case DXGI_FORMAT_R16G16_FLOAT:
			case DXGI_FORMAT_R16G16_UNORM:
			case DXGI_FORMAT_R16G16_UINT:
			case DXGI_FORMAT_R16G16_SNORM:
			case DXGI_FORMAT_R16G16_SINT:
			case DXGI_FORMAT_R32_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT:
			case DXGI_FORMAT_R32_FLOAT:
			case DXGI_FORMAT_R32_UINT:
			case DXGI_FORMAT_R32_SINT:
			case DXGI_FORMAT_R24G8_TYPELESS:
			case DXGI_FORMAT_D24_UNORM_S8_UINT:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
				return 32;
			case DXGI_FORMAT_R8G8_TYPELESS:
			case DXGI_FORMAT_R8G8_UNORM:
			case DXGI_FORMAT_R8G8_UINT:
			case DXGI_FORMAT_R8G8_SNORM:
			case DXGI_FORMAT_R8G8_SINT:
			case DXGI_FORMAT_R16_TYPELESS:
			case DXGI_FORMAT_R16_FLOAT:
			case DXGI_FORMAT_D16_UNORM:
			case DXGI_FORMAT_R16_UNORM:
			case DXGI_FORMAT_R16_UINT:
			case DXGI_FORMAT_R16_SNORM:
			case DXGI_FORMAT_R16_SINT:
				return 16;
			case DXGI_FORMAT_R8_TYPELESS:
			case DXGI_FORMAT_R8_UNORM:
			case DXGI_FORMAT_R8_UINT:
			case DXGI_FORMAT_R8_SNORM:
			case DXGI_FORMAT_R8_SINT:
			case DXGI_FORMAT_A8_UNORM:
				return 8;
			default:
				return 0;
			}
		}

		static RE::BSGraphics::Texture* thunk(RE::BSGraphics::Renderer* a_renderer, UINT a_width, UINT a_height, void* a_pixelData, D3D11_USAGE a_usage, DXGI_FORMAT a_format, bool a_uav) {
			// Texture Desc
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;

			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;

			desc.Format = a_format;
			desc.Usage = a_usage;

			// Bind flags
			if (a_usage == D3D11_USAGE_STAGING)
				desc.BindFlags = 0;
			else
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			if (a_uav)
				desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	
			// CPU access flags
			if (a_usage == D3D11_USAGE_DYNAMIC)
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			else if (a_usage == D3D11_USAGE_STAGING)
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
			else
				desc.CPUAccessFlags = 0;

			// ...
			desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

			D3D11_SUBRESOURCE_DATA initialData{};
			if (a_pixelData) {
				initialData.pSysMem = a_pixelData;
				initialData.SysMemPitch = a_width * (sub_140E4C440(a_format) >> 3);
				initialData.SysMemSlicePitch = 0;
			}

			auto* scrapHeap = RE::MemoryManager::GetSingleton()->GetThreadScrapHeap();
			auto* texture = reinterpret_cast<RE::BSGraphics::Texture*>(scrapHeap->Allocate(sizeof(RE::BSGraphics::Texture), 8));

			std::memset(texture, 0, sizeof(RE::BSGraphics::Texture));

			if (!texture)
				return nullptr;

			auto device = reinterpret_cast<ID3D11Device*>(a_renderer->GetDevice());

			HRESULT hr = device->CreateTexture2D(&desc, a_pixelData ? &initialData : nullptr, reinterpret_cast<ID3D11Texture2D**>(&texture->texture));

			if (FAILED(hr))
				return texture;

			// Store Texture Desc 
			auto* textureDesc = reinterpret_cast<RE::BSGraphics::TextureData*>(&texture->unk18);
			textureDesc->width = static_cast<uint16_t>(a_width);
			textureDesc->height = static_cast<uint16_t>(a_height);
			textureDesc->format = static_cast<uint8_t>(a_format);
			textureDesc->unk1C = 1;
			textureDesc->unk1E = 0;
			texture->unk20 = 1;

			// SRV creation (skipped for staging)
			if (a_usage != D3D11_USAGE_STAGING)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
				srvDesc.Format = a_format;
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = 1;
				srvDesc.Texture2D.MostDetailedMip = 0;

				device->CreateShaderResourceView(texture->texture, &srvDesc, &texture->resourceView);
			}
			else
			{
				texture->resourceView = nullptr;
			}

			return texture;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

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

	struct BGSObjectBlock_Load
	{
		static void thunk(RE::BGSObjectBlock* a_block, void* a_arg2, bool a_firstAvail)
		{
			if (!a_block->attached)
				Scene::GetSingleton()->GetSceneGraph()->CreateLODModel(a_block);

			func(a_block, a_arg2, a_firstAvail);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSObjectBlock_Attach
	{
		static void thunk(RE::BGSObjectBlock* a_block, void* a_arg2, bool a_firstAvail)
		{
			if (!a_block->attached)
				Scene::GetSingleton()->GetSceneGraph()->SetLODDetached(a_block, false);

			func(a_block, a_arg2, a_firstAvail);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSObjectBlock_Detach
	{
		static void thunk(RE::BGSObjectBlock* a_block)
		{
			if (a_block->attached)
				Scene::GetSingleton()->GetSceneGraph()->SetLODDetached(a_block, true);

			func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainBlock_Load
	{
		static void thunk(RE::BGSTerrainBlock* a_block)
		{
			if (a_block->loaded && !a_block->attached && a_block->chunk)
				Scene::GetSingleton()->GetSceneGraph()->CreateLODModel(a_block);

			func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainBlock_Attach
	{
		static void thunk(RE::BGSTerrainBlock* a_block)
		{
			if (a_block->loaded && !a_block->attached && a_block->chunk)
				Scene::GetSingleton()->GetSceneGraph()->SetLODDetached(a_block, false);

			func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainBlock_Detach
	{
		static RE::BSMultiBoundNode* thunk(RE::BGSTerrainBlock* a_block)
		{
			if (a_block->attached)
				Scene::GetSingleton()->GetSceneGraph()->SetLODDetached(a_block, true);

			return func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct LoadAndAttachAddon
	{
		static RE::NiAVObject* thunk(RE::TESModel* a_model, RE::BIPED_OBJECT a_bipedObj, RE::TESObjectREFR* a_actor, RE::BSTSmartPointer<RE::BipedAnim>& a_biped, RE::NiAVObject* a_root)
		{
			auto* result = func(a_model, a_bipedObj, a_actor, a_biped, a_root);

			if (auto* animObject = stl::adjust_pointer<RE::TESObjectANIO>(a_model->GetAsModelTextureSwap(), -0x20); animObject) {
				auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

				if (auto* actorRefr = sceneGraph->GetActorRefr(a_actor->GetFormID())) {
					actorRefr->AttachAnimObject(animObject, result);
				}
			}
			
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DetachAddonSE
	{
		static int32_t thunk(RE::AnimationObject* a_animObject)
		{
			if (a_animObject) {
				if (auto refrPtr = a_animObject->handle.get()) {
					if (auto* object = a_animObject->object) {
						auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

						if (auto* actorRefr = sceneGraph->GetActorRefr(refrPtr->GetFormID())) {
							actorRefr->DetachAnimObject(object);
						}
					}
				}
			}

			return func(a_animObject);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DetachAddonAE
	{
		static int32_t thunk(RE::BSTArray<RE::AnimationObject>& a_animObjects, uint32_t a_index, uint32_t a3)
		{
			if (a3) {
				auto& animObject = a_animObjects[a_index];

				if (auto refrPtr = animObject.handle.get()) {
					if (auto* object = animObject.object) {
						auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

						if (auto* actorRefr = sceneGraph->GetActorRefr(refrPtr->GetFormID())) {
							actorRefr->DetachAnimObject(object);
						}
					}
				}
			}

			return func(a_animObjects, a_index, a3);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

#elif defined(FALLOUT4)

#endif

	void Install()
	{
		stl::write_vfunc<0x0, NiSourceTexture_Destructor>(RE::VTABLE_NiSourceTexture[0]);

#if defined(SKYRIM)
		stl::detour_thunk<CreateTextureAndSRV>(REL::RelocationID(75724, 77538));

		stl::detour_thunk<CreateRenderTarget>(REL::RelocationID(75467, 77253));
		stl::detour_thunk<CreateDepthStencil>(REL::RelocationID(75469, 77255));

		stl::detour_thunk<TES_AttachModel>(REL::RelocationID(13209, 13355));
		//stl::write_vfunc<0x6B, Release3DRelatedData>(RE::VTABLE_TESObjectREFR[0]);
		stl::detour_thunk<TESObject_UnClone3D>(REL::RelocationID(17249, 17642));

		//stl::detour_thunk<Actor_Set3D>(REL::RelocationID(36199, 37178));

		stl::write_thunk_call<BGSTerrainBlock_Load>(REL::RelocationID(31090, 31888).address() + REL::Relocate(0x11, 0x11));
		stl::detour_thunk<BGSTerrainBlock_Attach>(REL::RelocationID(30934, 31737));
		stl::detour_thunk<BGSTerrainBlock_Detach>(REL::RelocationID(30936, 31739));

		stl::write_thunk_call<BGSObjectBlock_Load>(REL::RelocationID(31100, 31908).address() + REL::Relocate(0x5c, 0x49));
		stl::detour_thunk<BGSObjectBlock_Attach>(REL::RelocationID(30741, 31581));
		stl::detour_thunk<BGSObjectBlock_Detach>(REL::RelocationID(30739, 31577));

		// Landscape
		stl::detour_thunk<TESObjectLAND_Attach3D>(REL::RelocationID(18334, 18750));
		stl::detour_thunk<TESObjectLAND_Detach3D>(REL::RelocationID(18335, 18751));

		// Water
		stl::detour_thunk<TESWaterSystem_AddWater>(REL::RelocationID(31388, 32179));
		stl::detour_thunk<TESWaterSystem_RemoveWater>(REL::RelocationID(31391, 32182));

		stl::write_thunk_call<CreateFlowMap>(REL::RelocationID(31234, 32031).address() + REL::Relocate(0x7E, 0xF8));

		stl::write_vfunc<0x18, BSCullingProcess_AppendVirtual>(RE::VTABLE_BSCullingProcess[0]);
		stl::write_vfunc<0x18, BSFadeNodeCuller_AppendVirtual>(RE::VTABLE_BSFadeNodeCuller[0]);
		stl::write_vfunc<0x18, NiCullingProcess_AppendVirtual>(RE::VTABLE_NiCullingProcess[0]);

		stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

		auto* scene = Scene::GetSingleton();
		scene->g_FlowMapSize = reinterpret_cast<int32_t*>(REL::RelocationID(527644, 414596).address());
		scene->g_FlowMapSourceTex = reinterpret_cast<RE::NiPointer<RE::NiSourceTexture>*>(REL::RelocationID(527694, 414616).address());
		scene->g_DisplacementCellTexCoordOffset = reinterpret_cast<float4*>(REL::RelocationID(528184, 415129).address());
		scene->g_DisplacementMeshFlowCellOffset = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(528164, 415109).address());
		
		stl::write_thunk_call<LoadAndAttachAddon>(REL::RelocationID(42420, 43576).address() + REL::Relocate(0x22A, 0x21F));

		if (REL::Module::IsSE())
			stl::detour_thunk<DetachAddonSE>(REL::RelocationID(42412, 0));
		else
			stl::detour_thunk<DetachAddonAE>(REL::RelocationID(0, 43585));

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
