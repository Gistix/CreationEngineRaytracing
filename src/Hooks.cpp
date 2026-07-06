#include "Hooks.h"
#include "Renderer.h"
#include "Util.h"

#include "Core/D3D12Texture.h"
#include "Core/DynamicMesh.h"

namespace Hooks
{
	struct ID3D11Device_CreateBuffer
	{
		static HRESULT thunk(ID3D11Device* a_device, const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer)
		{
			if (!pDesc)
				return func(a_device, pDesc, pInitialData, ppBuffer);

			D3D11_BUFFER_DESC desc = *pDesc;

			if (desc.Usage == D3D11_USAGE_DEFAULT && desc.CPUAccessFlags == 0 && (desc.BindFlags & D3D11_BIND_VERTEX_BUFFER || desc.BindFlags & D3D11_BIND_INDEX_BUFFER))
				desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;

			auto hr = func(a_device, &desc, pInitialData, ppBuffer);

			if (FAILED(hr)) {
				logger::error("ID3D11Device::CreateBuffer - Failed with HR: 0x{:08X}", static_cast<UINT>(hr));
				hr = func(a_device, pDesc, pInitialData, ppBuffer);
			}

			return hr;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct MemoryManager_AllocateTriShape
	{
		static RE::BSGraphics::TriShapeDX12* thunk(RE::MemoryManager* a_memoryManager, [[ maybe_unused ]] size_t size, int32_t a_alignment, bool a_alignmentRequired)
		{
			return func(a_memoryManager, sizeof(RE::BSGraphics::TriShapeDX12), a_alignment, a_alignmentRequired);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_CreateTriShape
	{
		static RE::BSGraphics::TriShapeDX12* thunk(
			RE::BSGraphics::Renderer* a_renderer,
			RE::BSStream* a_bsStream,
			RE::BSGraphics::VertexDesc a_vertexDesc,
			uint16_t a_vertexCount, 
			uint32_t a_indexCount)
		{
			auto triShape = func(a_renderer, a_bsStream, a_vertexDesc, a_vertexCount, a_indexCount);

			// Share vertex buffer
			Util::CreateSharedBuffer(triShape->vertexBuffer, &triShape->vertexBufferDX12);
	
			// Share index buffer
			Util::CreateSharedBuffer(triShape->indexBuffer, &triShape->indexBufferDX12);

			return triShape;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_CreateTriShapeParticles
	{
		static RE::BSGraphics::TriShapeDX12* thunk(
			RE::BSGraphics::Renderer* a_renderer,
			uint8_t* vertexData, 
			uint32_t vertexDataSize,
			RE::BSGraphics::VertexDesc vertexDesc,
			uint16_t* indexData, 
			uint32_t numIndices)
		{
			auto triShape = func(a_renderer, vertexData, vertexDataSize, vertexDesc, indexData, numIndices);

			// Share vertex buffer
			Util::CreateSharedBuffer(triShape->vertexBuffer, &triShape->vertexBufferDX12);

			// Share index buffer
			Util::CreateSharedBuffer(triShape->indexBuffer, &triShape->indexBufferDX12);

			return triShape;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_CreateTriShapeVertex
	{
		struct IndexRenderData
		{
			ID3D11Buffer* indexBuffer;
			int32_t refCount;
		};

		static RE::BSGraphics::TriShapeDX12* thunk(
			RE::BSGraphics::Renderer* a_renderer,
			uint8_t* a_vertexData,
			uint32_t a_vertexDataSize,
			RE::BSGraphics::VertexDesc a_vertexDesc,
			IndexRenderData* a_indexRenderData)
		{
			auto triShape = func(a_renderer, a_vertexData, a_vertexDataSize, a_vertexDesc, a_indexRenderData);

			// Share vertex buffer
			Util::CreateSharedBuffer(triShape->vertexBuffer, &triShape->vertexBufferDX12);

			// The original code does 'triShape->indexBuffer = a_indexRenderData->indexBuffer' and calls 'AddRef'
			// We have no way of copying the original indexBufferDX12 here, so we just share it again
			Util::CreateSharedBuffer(triShape->indexBuffer, &triShape->indexBufferDX12);

			return triShape;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_CreateTriShapeIndex
	{
		struct VertexRenderData
		{
			ID3D11Buffer* vertexBuffer;				// 00
			ID3D11Buffer* indexBuffer;				// 08
			RE::BSGraphics::VertexDesc vertexDesc;  // 10
		};

		static RE::BSGraphics::TriShapeDX12* thunk(
			RE::BSGraphics::Renderer* a_renderer,
			VertexRenderData* a_vertexRenderData,
			RE::BSGraphics::VertexDesc vertexDesc,
			uint16_t* a_indexData,
			uint32_t a_numIndices)
		{
			auto triShape = func(a_renderer, a_vertexRenderData, vertexDesc, a_indexData, a_numIndices);

			// Share vertex buffer
			// The original function utilizes 'BSGraphics::CopyTriShapeVertices' to copy from 'VertexRenderData' into 'RE::BSGraphics::TriShape'
			// TODO: Find all sites where 'VertexRenderData' is created and extend it with DX12 buffers as well
			Util::CreateSharedBuffer(triShape->vertexBuffer, &triShape->vertexBufferDX12);

			// Share index buffer
			Util::CreateSharedBuffer(triShape->indexBuffer, &triShape->indexBufferDX12);

			return triShape;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Releases dst vertexBuffer, copies src vertexBuffer pointer and calls 'AddRef'
	// Frees dst rawVertexData, then allocates memory for src rawVertexData
	struct BSGraphics_CopyTriShapeVertices
	{
		static int32_t thunk(
			RE::BSGraphics::Renderer* a_renderer,
			BSGraphics_CreateTriShapeIndex::VertexRenderData* a_dstTriShape,
			BSGraphics_CreateTriShapeIndex::VertexRenderData* a_srcTriShape)
		{
			return func(a_renderer, a_dstTriShape, a_srcTriShape);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSTriShape_Dtor
	{
		static void thunk(RE::BSTriShape* a_bsTriShape, bool a_release)
		{
			Scene::GetSingleton()->GetSceneGraph()->OnDestroy(a_bsTriShape);

			func(a_bsTriShape, a_release);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSDynamicTriShape_Dtor
	{
		static void thunk(RE::BSDynamicTriShape* a_bsDynamicTriShape, bool a_release)
		{
			logger::info("BSDynamicTriShape::Dtor - {}", fmt::ptr(a_bsDynamicTriShape));

			Scene::GetSingleton()->GetSceneGraph()->OnDestroy(a_bsDynamicTriShape);

			func(a_bsDynamicTriShape, a_release);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Copies new float3* dynamic data to the dynamic trishape's float4* dynamic data
	struct BSDynamicTriShape_UpdateDynamicData
	{
		static void thunk(RE::BSDynamicTriShape* a_bsDynamicTriShape, float3* a_dynamicData, bool a_recalculateBounds)
		{
			func(a_bsDynamicTriShape, a_dynamicData, a_recalculateBounds);

			// Dynamic data is ready as float4* after the original function ran
			Scene::GetSingleton()->GetSceneGraph()->UpdateDynamicData(a_bsDynamicTriShape);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TriShape_Dtor
	{
		static void thunk([[ maybe_unused ]] void* a1, RE::BSGraphics::TriShape* a_triShape)
		{
			if (a_triShape && _InterlockedExchangeAdd(&a_triShape->refCount, 0xFFFFFFFF) == 1)
			{
				auto indexBuffer = reinterpret_cast<ID3D11Buffer*>(a_triShape->indexBuffer);
				if (indexBuffer)
					indexBuffer->Release();

				auto vertexBuffer = reinterpret_cast<ID3D11Buffer*>(a_triShape->vertexBuffer);
				if (vertexBuffer)
					vertexBuffer->Release();

				auto* mm = RE::MemoryManager::GetSingleton();

				if (a_triShape->rawVertexData)
					mm->Deallocate(a_triShape->rawVertexData, false);

				if (a_triShape->rawIndexData)
					mm->Deallocate(a_triShape->rawIndexData, false);

				if (a_triShape->pad1C == 1) {
					auto* triShapeDX12 = static_cast<RE::BSGraphics::TriShapeDX12*>(a_triShape);

					if (triShapeDX12->indexBufferDX12)
						triShapeDX12->indexBufferDX12->Release();

					if (triShapeDX12->vertexBufferDX12)
						triShapeDX12->vertexBufferDX12->Release();
				}

				mm->Deallocate(a_triShape, false);
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSTextureSet_SetTexture
	{
		static void thunk(RE::BSTextureSet* a_bsTextureSet, RE::BSTextureSet::Texture a_texture, RE::NiSourceTexturePtr& a_srcTexture)
		{
			func(a_bsTextureSet, a_texture, a_srcTexture);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};


	void NiSourceTexture_Destructor::thunk(RE::NiSourceTexture* oThis)
	{
#if defined(SKYRIM)
		if (oThis && oThis->rendererTexture) {
			auto scene = Scene::GetSingleton();
			auto sceneGraph = scene->GetSceneGraph();
			sceneGraph->ReleaseTexture(oThis->rendererTexture);
		}
#endif

		func(oThis);
	}

#if defined(SKYRIM)
	struct BSGraphicsTexture_Dtor
	{
		static void thunk(void* a1, RE::BSGraphics::Texture* a_texture)
		{
			if (a_texture->pad24 == NO_DX12RESOURCE)
				func(a1, a_texture);

			if (InterlockedExchangeAdd(&a_texture->refCount, 0xFFFFFFFF) == 1)
			{
				auto* d3d12Texture = reinterpret_cast<RE::BSGraphics::D3D12Texture*>(a_texture);

				if (d3d12Texture->d3d12Texture)
					d3d12Texture->d3d12Texture->Release();

				if (d3d12Texture->resourceView)
					d3d12Texture->resourceView->Release();

				if (d3d12Texture->texture)
					d3d12Texture->texture->Release();

				if (d3d12Texture->UAV)
					d3d12Texture->UAV->Release();

				auto* scrapHeap = RE::MemoryManager::GetSingleton()->GetThreadScrapHeap();

				// Doesn't take size to be freed?
				scrapHeap->Deallocate(d3d12Texture);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

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

		if (!shareTexture) {
			auto result = func(a_device, a_dimension, a_width, a_height, a_depth, a_mipLevels, a_arraySize, a_format, a_cubemap, a_data, a_outTexture);

			// Enforce flag
			if (SUCCEEDED(result))
				(*a_outTexture)->pad24 = NO_DX12RESOURCE;

			return result;
		}

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
			texture->UAV = defaultTexture->UAV;
			texture->resourceView = defaultTexture->resourceView;
			texture->width = defaultTexture->width;
			texture->height = defaultTexture->height;
			texture->format = defaultTexture->format;
			texture->mips = defaultTexture->mips;
			texture->unk1E = defaultTexture->unk1E;
			texture->refCount = defaultTexture->refCount;

			defaultTexture->texture->AddRef();
			defaultTexture->resourceView->AddRef();

			// We use this as a flag to indicate this 'Texture' is actually 'D3D12Texture'
			texture->pad24 = NATIVE_DX12RESOURCE;

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

			auto format = Renderer::GetFormat(a_format);
			if (format == nvrhi::Format::UNKNOWN) {
				if (texture->d3d12Texture)
					texture->d3d12Texture->Release();

				return E_FAIL;
			}

			auto& textureDesc = nvrhi::TextureDesc()
				.setWidth(a_width)
				.setHeight(a_height)
				.setMipLevels(a_mipLevels)
				.setFormat(format)
				.setInitialState(nvrhi::ResourceStates::CopyDest)
				.setDebugName("Shared Texture [?]");

			auto textureHandle = Renderer::GetSingleton()->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(texture->d3d12Texture), textureDesc);

			// Upload Texture Data
			{
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

			texture->pad24 = NO_DX12RESOURCE;

			*a_outTexture = texture;

			return result;
		}
	}

	struct CreateTexture2DAndSRV
	{
		static uint32_t GetBitsPerPixel(DXGI_FORMAT a_format)
		{
			auto fmt = static_cast<uint32_t>(a_format);
			if (fmt >= 1 && fmt <= 4)       return 128;
			if (fmt >= 5 && fmt <= 8)       return 96;
			if (fmt >= 9 && fmt <= 22)      return 64;
			if (fmt >= 23 && fmt <= 45)     return 32;
			if (fmt == 87)                  return 32;
			if (fmt >= 48 && fmt <= 59)     return 16;
			if (fmt >= 60 && fmt <= 65)     return 8;
			return 0;
		}

		static RE::BSGraphics::Texture* thunk(
			RE::BSGraphics::Renderer* a_renderer,
			uint32_t a_width,
			uint32_t a_height,
			void* a_pixelData,
			D3D11_USAGE a_usage,
			DXGI_FORMAT a_format,
			bool a_unorderedAccess)
		{
			auto* scrapHeap = RE::MemoryManager::GetSingleton()->GetThreadScrapHeap();

			auto* texture = reinterpret_cast<RE::BSGraphics::Texture*>(
				scrapHeap->Allocate(sizeof(RE::BSGraphics::Texture), 8));

			if (!texture)
				return nullptr;

			std::memset(texture, 0, sizeof(RE::BSGraphics::Texture));

			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = a_format;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Usage = a_usage;
			desc.BindFlags = (a_usage == D3D11_USAGE_STAGING) ? 0 : D3D11_BIND_SHADER_RESOURCE;

			if (a_unorderedAccess)
				desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

			if (a_usage == D3D11_USAGE_DYNAMIC)
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			else if (a_usage == D3D11_USAGE_STAGING)
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
			else
				desc.CPUAccessFlags = 0;

			desc.MiscFlags = (a_usage == 0 || a_usage == D3D11_USAGE_IMMUTABLE) ? D3D11_RESOURCE_MISC_SHARED : 0;

			D3D11_SUBRESOURCE_DATA subresData = {};
			D3D11_SUBRESOURCE_DATA* pSubres = nullptr;
			if (a_pixelData) {
				auto bpp = GetBitsPerPixel(a_format);
				subresData.pSysMem = a_pixelData;
				subresData.SysMemPitch = (bpp >> 3) * a_width;
				subresData.SysMemSlicePitch = 0;
				pSubres = &subresData;
			}

			auto device = reinterpret_cast<ID3D11Device*>(a_renderer->GetDevice());

			ID3D11Texture2D* d3dTex = nullptr;
			HRESULT hr = device->CreateTexture2D(&desc, pSubres, &d3dTex);

			if (FAILED(hr) || !d3dTex)
				return nullptr;

			texture->texture = d3dTex;
			texture->width = static_cast<uint16_t>(a_width);
			texture->height = static_cast<uint16_t>(a_height);
			texture->format = static_cast<uint8_t>(a_format);
			texture->mips = 1;
			texture->unk1E = 0;
			texture->refCount = 1;
			texture->pad24 = NO_DX12RESOURCE;

			if (a_usage != D3D11_USAGE_STAGING) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = a_format;
				srvDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = 1;

				device->CreateShaderResourceView(d3dTex, &srvDesc, &texture->resourceView);
			}

			return texture;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

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

	void NiCullingProcess_AppendVirtual::thunk(RE::NiCullingProcess* cullingProcess, RE::BSGeometry* geometry, uint32_t a_arg2)
	{
		if (geometry) {
			if (Scene::GetSingleton()->ApplyPathTracingCull() && Util::Culling::ShouldCull(geometry))
				return;
		}

		func(cullingProcess, geometry, a_arg2);
	}

	void BSBatchRenderer_RenderPassImmediately::thunk(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags)
	{
		if (!pass->shader) {
			func(pass, technique, alphaTest, renderFlags);
			return;
		}

		const auto shaderType = pass->shader->shaderType.get();
		auto* scene = Scene::GetSingleton();

		const bool pathTracingActive = scene->IsPathTracingActive();

		// Water rendering toggle during path tracing
		if (pathTracingActive && shaderType == RE::BSShader::Type::Water)
			return;

		auto* shaderProperty = pass->shaderProperty;
		if (!shaderProperty || !shaderProperty->material) {
			func(pass, technique, alphaTest, renderFlags);
			return;
		}

		if (pathTracingActive) {
			// Cull non-effect refractive geometry during path tracing
			if (shaderType != RE::BSShader::Type::Effect && shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kRefraction))
				return;

			auto feature = shaderProperty->material->GetFeature();

			// Cull eyes since they are transparent and draw after PT is composited
			switch (feature) {
			case RE::BSShaderMaterial::Feature::kEnvironmentMap: // Some mods use this flag for eyes,
			case RE::BSShaderMaterial::Feature::kEye:
				return;
			}
		}

		// Path tracing occlusion-based culling
		if (scene->ApplyPathTracingCull() && pass->shader && pass->geometry)
		{
			switch (shaderType) {
			case RE::BSShader::Type::Sky:
			case RE::BSShader::Type::Water:
			case RE::BSShader::Type::Effect:
			case RE::BSShader::Type::Particle:
				break;
			default:
				// Utility, Lighting, DistantTree, BloodSplatter, etc.
				return;
			}
		}

		func(pass, technique, alphaTest, renderFlags);
	}

	struct BGSTerrainBlock_Load
	{
		static RE::BGSTerrainBlock* thunk(RE::BGSTerrainBlock* a_block, RE::BGSTerrainManager* a_terrainManager, RE::BSStream* a_stream, int32_t a4, int32_t a5)
		{
			auto result = func(a_block, a_terrainManager, a_stream, a4, a5);

			if (result && result->node && result->land) {
				auto lodLevel = static_cast<std::int32_t>(result->node->GetLODLevel());
				auto key = RE::BSFixedString(Constants::ExtraData::LandLOD);
				auto extra = RE::NiIntegersExtraData::Create(key, { lodLevel });
				result->land->AddExtraData(extra);
			}

			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainBlock_Dtor
	{
		static void thunk(RE::BGSTerrainBlock* a_block)
		{
			func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSObjectBlock_Load
	{
		static RE::BGSObjectBlock* thunk(RE::BGSObjectBlock* a_block, RE::BGSTerrainNode* a_terrainNode, RE::BSStream* a_stream)
		{
			auto result = func(a_block, a_terrainNode, a_stream);
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSObjectBlock_Dtor
	{
		static void thunk(RE::BGSObjectBlock* a_block)
		{
			return func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSDistantTreeBlock_AttachSE
	{
		static void thunk(RE::BGSDistantTreeBlock* a_block, float a2)
		{
			const bool wasAttached = a_block->attached;

			func(a_block, a2);

			const bool valid = a_block->node && a_block->attached && !wasAttached && !a_block->node->mapTerrain;
			if (a_block->doneLoading && valid) {
				if (!a_block->treeGroups.empty()) {
				}
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSDistantTreeBlock_AttachAE
	{
		static void thunk(RE::BGSDistantTreeBlock* a_block)
		{
			const bool wasAttached = a_block->attached;

			func(a_block);

			const bool valid = a_block->node && a_block->attached && !wasAttached && !a_block->node->mapTerrain;
			if (a_block->doneLoading && valid) {
				if (!a_block->treeGroups.empty()) {

				}
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSDistantTreeBlock_DtorSE
	{
		static void thunk(RE::BGSDistantTreeBlock* a_block)
		{
			func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSDistantTreeBlock_DtorAE
	{
		static_assert(sizeof(RE::BGSTerrainNode::Layer<RE::BGSDistantTreeBlock>) == 0x30);

		static void thunk(RE::BSResource::IEntryDB* a_entryDB, RE::BGSTerrainNode::Layer<RE::BGSDistantTreeBlock>* a2, int a3, void* a4)
		{
			RE::BGSDistantTreeBlock* block = nullptr;

			if (a2)
				block = a2->block;

			func(a_entryDB, a2, a3, a4);

			if (a2 && block) {
				if (block != a2->block) {
				}
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};
	
	// 1401B6AF0 SE/140203810 AE
	struct GrassManager_CreateInstances
	{
		static uint32_t thunk(RE::BGSGrassManager* a_grassManager, RE::CreateGrassParams* a_createGrassParams)
		{
			auto instances = func(a_grassManager, a_createGrassParams);

			if (instances > 0) {
			}

			return instances;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

#elif defined(FALLOUT4)

#endif

	void Install()
	{
		stl::write_vfunc<0x0, NiSourceTexture_Destructor>(RE::VTABLE_NiSourceTexture[0]);

#if defined(SKYRIM)
		const auto createTriShapeA = REL::RelocationID(75473, 77259);
		const auto createTriShapeB = REL::RelocationID(75474, 77260);
		const auto createTriShapeC = REL::RelocationID(75475, 77261);
		const auto createTriShapeD = REL::RelocationID(75476, 77262);

		stl::write_thunk_call<MemoryManager_AllocateTriShape>(createTriShapeA.address() + 0x9f);
		stl::write_thunk_call<MemoryManager_AllocateTriShape>(createTriShapeB.address() + 0x87);
		stl::write_thunk_call<MemoryManager_AllocateTriShape>(createTriShapeC.address() + 0x82);
		stl::write_thunk_call<MemoryManager_AllocateTriShape>(createTriShapeD.address() + REL::Relocate(0x85, 0x87));

		stl::detour_thunk<BSGraphics_CreateTriShape>(createTriShapeA);
		stl::detour_thunk<BSGraphics_CreateTriShapeParticles>(createTriShapeB);
		stl::detour_thunk<BSGraphics_CreateTriShapeVertex>(createTriShapeC); // Landscape
		stl::detour_thunk<BSGraphics_CreateTriShapeIndex>(createTriShapeD);  // NiSkinPartition::Partition::buffData

		// This function is inlined in some places on AE
		//stl::detour_thunk<BSGraphics_CopyTriShapeVertices>(REL::RelocationID(74735, 76477));

		stl::detour_thunk<BSTriShape_Dtor>(REL::RelocationID(69294, 70666));  // BSTriShape
		//stl::detour_thunk<BSDynamicTriShape_Dtor>(REL::RelocationID(69573, 0));  // BSDynamicTriShape
		//stl::detour_thunk<BSTriShape_Dtor>(REL::RelocationID(75033, 0));  // BSSubIndexTriShape
		//stl::detour_thunk<BSTriShape_Dtor>(REL::RelocationID(74611, 0));  // BSMultiStreamInstanceTriShape

		// Use a hook to update dynamic data, else we risk trying accessing dynamic data while the engine has already released it
		stl::detour_thunk<BSDynamicTriShape_UpdateDynamicData>(REL::RelocationID(69570, 70954));
		
		stl::detour_thunk<TriShape_Dtor>(REL::RelocationID(75480, 77267));
		
		stl::detour_thunk<BSTextureSet_SetTexture>(REL::RelocationID(20907, 0));
		
		// All textures loaded from DDS
		stl::detour_thunk<CreateTextureAndSRV>(REL::RelocationID(75724, 77538));

		// Flowmap and Player FaceGen Tint
		stl::detour_thunk<CreateTexture2DAndSRV>(REL::RelocationID(75511, 77303));

		//stl::detour_thunk<BSGraphicsTexture_Dtor>(REL::RelocationID(75527, 77322));

		stl::detour_thunk<CreateRenderTarget>(REL::RelocationID(75467, 77253));
		stl::detour_thunk<CreateDepthStencil>(REL::RelocationID(75469, 77255));

		// Terrain LOD
		stl::detour_thunk<BGSTerrainBlock_Load>(REL::RelocationID(30932, 31735));
		//stl::detour_thunk<BGSTerrainBlock_Dtor>(REL::RelocationID(30933, 31736));

		// Object LOD
		//stl::detour_thunk<BGSObjectBlock_Load>(REL::RelocationID(30737, 31575));

		// Two completely different functions for SE and AE, however the end hook address for both is NiMemFree
		//stl::write_thunk_call<BGSObjectBlock_Dtor>(REL::RelocationID(30730, 31634).address() + REL::Relocate(0x6D, 0x11A));

		// Tree LOD
		/*if (REL::Module::IsSE()) {
			stl::detour_thunk<BGSDistantTreeBlock_AttachSE>(REL::RelocationID(30832, 0));
			stl::detour_thunk<BGSDistantTreeBlock_DtorSE>(REL::RelocationID(30821, 0));
		}
		else {
			stl::detour_thunk<BGSDistantTreeBlock_AttachAE>(REL::RelocationID(0, 31653));
			stl::detour_thunk<BGSDistantTreeBlock_DtorAE>(REL::RelocationID(0, 31717));
		}*/
		
		// Landscape
		//stl::detour_thunk<TESObjectLAND_Attach3D>(REL::RelocationID(18334, 18750));
		//stl::detour_thunk<TESObjectLAND_Detach3D>(REL::RelocationID(18335, 18751));

		// Water
		//stl::detour_thunk<TESWaterSystem_AddWater>(REL::RelocationID(31388, 32179));
		//stl::detour_thunk<TESWaterSystem_RemoveWater>(REL::RelocationID(31391, 32182));

		// Grass
		//stl::detour_thunk<GrassManager_CreateInstances>(REL::RelocationID(15212, 15381));

		/*stl::detour_thunk<NiCullingProcess_AppendVirtual>(REL::RelocationID(26533, 27130));
		stl::detour_thunk<BSCullingProcess_AppendVirtual>(REL::RelocationID(74807, 76556));*/

		stl::write_vfunc<0x18, NiCullingProcess_AppendVirtual>(RE::VTABLE_NiCullingProcess[0]);
		stl::write_vfunc<0x18, NiCullingProcess_AppendVirtual>(RE::VTABLE_BSFadeNodeCuller[0]);
		stl::write_vfunc<0x18, NiCullingProcess_AppendVirtual>(RE::VTABLE_BSCullingProcess[0]);

		stl::detour_thunk<BSBatchRenderer_RenderPassImmediately>(REL::RelocationID(100854, 107644));

		auto* scene = Scene::GetSingleton();
		scene->g_FlowMapSize = reinterpret_cast<int32_t*>(REL::RelocationID(527644, 414596).address());
		scene->g_DisplacementCellTexCoordOffset = reinterpret_cast<float4*>(REL::RelocationID(528184, 415129).address());
		scene->g_DisplacementMeshFlowCellOffset = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(528164, 415109).address());

		REL::Relocation<float*> g_Time{ REL::RelocationID(513213, 390953) };
		scene->g_Time = g_Time.get();

		scene->g_TreeLODAtlasTex = reinterpret_cast<RE::NiPointer<RE::NiSourceTexture>*>(REL::RelocationID(528222, 415172).address());

		REL::Relocation<bool*> g_BypassSubIndexVisibility{ REL::RelocationID(524687, 411302) };
		scene->g_BypassSubIndexVisibility = g_BypassSubIndexVisibility.get();

#elif defined(FALLOUT4)
#	if defined(FALLOUT_POST_NG)
		stl::detour_thunk<TES_AttachModel>(REL::ID(2192085));
#	endif
#endif
		logger::info("[Raytracing] Installed hooks");
	}

	void InstallD3D11(ID3D11Device* a_device)
	{
		stl::detour_vfunc<3, ID3D11Device_CreateBuffer>(a_device);
	}
}
