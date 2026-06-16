#include "RenderTargetManager.h"
#include "Renderer.h"

nvrhi::ITexture* RenderTargetManager::GetTexture(Texture texture, uint32_t slot) {
	auto& renderTarget = m_Textures[slot][static_cast<size_t>(texture)];

	if (!renderTarget.handle) {
		auto* renderer = Renderer::GetSingleton();
		auto device = renderer->GetDevice();

		auto resolution = renderer->GetResolution();
		nvrhi::TextureDesc desc{};

		// Set default values
		desc.width = resolution.x;
		desc.height = resolution.y;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.initialState = nvrhi::ResourceStates::Common;
		desc.isUAV = true;
		desc.keepInitialState = true;

		switch (texture)
		{
		case RenderTarget::Main:
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.sharedResourceFlags = nvrhi::SharedResourceFlags::Shared;
			break;
		case RenderTarget::Accumulation:
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			break;
		case RenderTarget::ViewDepth:
		case RenderTarget::ClipDepth:
			desc.format = nvrhi::Format::R32_FLOAT;
			break;
		case RenderTarget::FaceNormals:
			desc.format = nvrhi::Format::R11G11B10_FLOAT;
			break;
		case RenderTarget::MotionVectors3D:
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			break;			
		case RenderTarget::DiffuseAlbedo:
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.sharedResourceFlags = nvrhi::SharedResourceFlags::Shared;
			break;
		case RenderTarget::DiffuseRadiance:
		case RenderTarget::SpecularRadiance:
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			break;
		case RenderTarget::DiffuseFactor:
		case RenderTarget::SpecularFactor:  // RRSpecularAlbedo
			desc.format = nvrhi::Format::R11G11B10_FLOAT;
			break;
		case RenderTarget::RRSpecularHitDist:
			desc.format = nvrhi::Format::R32_FLOAT;
			break;
		default:
			break;
		}

		desc.debugName = std::format("{}_{}", magic_enum::enum_name(texture), slot);

		logger::debug("RenderTargetManager::GetTexture - Slot: {}, Dimensions: [{}, {}], Format: {}, Shared: {} - {}", 
			slot, desc.width, desc.height,
			magic_enum::enum_name(desc.format), 
			(desc.sharedResourceFlags & nvrhi::SharedResourceFlags::Shared) != nvrhi::SharedResourceFlags::None,
			desc.debugName);

		if ((desc.sharedResourceFlags & nvrhi::SharedResourceFlags::Shared) == 0)
			renderTarget.handle = device->createTexture(desc);
		else {
			D3D12_RESOURCE_DESC nativeDesc = nvrhi::d3d12::convertTextureDesc(desc);

			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

			D3D12_RESOURCE_STATES state = nvrhi::d3d12::convertResourceStates(desc.initialState);

			// Legacy shared textures have better format support, but require using compatibility device which may be unavailable (especially when using VKD3D)
			auto compatDevice = Renderer::GetCompatDevice();
			if (compatDevice) {
				D3D11_RESOURCE_FLAGS flags11{};
				flags11.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

				if (nativeDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
					flags11.BindFlags |= D3D11_BIND_RENDER_TARGET;

				if (nativeDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
					flags11.BindFlags |= D3D11_BIND_DEPTH_STENCIL;

				if (nativeDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
					flags11.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

				if (!(nativeDesc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
					flags11.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

				auto hr = compatDevice->CreateSharedResource(
					&heapProps,
					D3D12_HEAP_FLAG_SHARED,
					&nativeDesc,
					state,
					nullptr,
					&flags11,
					D3D12_COMPATIBILITY_SHARED_FLAG_NON_NT_HANDLE,
					nullptr,
					nullptr,
					IID_PPV_ARGS(renderTarget.d3d12Resource.put()));

				if (FAILED(hr)) {
					logger::info("RenderTargetManager::GetTexture - Create shared resource failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
					return nullptr;
				}

				renderTarget.handle = device->createHandleForNativeTexture(
					nvrhi::ObjectTypes::D3D12_Resource,
					nvrhi::Object(renderTarget.d3d12Resource.get()),
					desc);

				hr = compatDevice->ReflectSharedProperties(
					renderTarget.d3d12Resource.get(),
					D3D12_REFLECT_SHARED_PROPERTY_NON_NT_SHARED_HANDLE,
					&renderTarget.sharedHandle, sizeof(HANDLE));

				if (FAILED(hr)) {
					logger::info("RenderTargetManager::GetTexture - Reflect shared properties failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
					return nullptr;
				}
			}
			else {
				logger::info("RenderTargetManager::GetTexture - D3D12 Compatibility Device is not available, falling back to standard NT shared texture");

				// Add Shared NT Handle flag
				desc.sharedResourceFlags |= nvrhi::SharedResourceFlags::Shared_NTHandle;

				renderTarget.handle = device->createTexture(desc);
				renderTarget.sharedHandle = renderTarget.handle->getNativeObject(nvrhi::ObjectTypes::SharedHandle);
			}
		}		
	}

	return renderTarget.handle;
}

nvrhi::ITexture* RenderTargetManager::GetTexture(Texture texture) {
	return GetTexture(texture, Renderer::GetSingleton()->GetCurrentSlot());
}

SharedTexture RenderTargetManager::GetSharedTexture(Texture texture, uint32_t slot) {
	SharedTexture sharedTexture;

	auto* dx12Texture = GetTexture(texture, slot);
	if (!dx12Texture) {
		logger::info("RenderTargetManager::GetSharedTexture - Invalid texture for {}", magic_enum::enum_name(texture));
		return sharedTexture;
	}

	auto& renderTarget = m_Textures[slot][static_cast<size_t>(texture)];

	if (!renderTarget.d3d11Texture) {
		if (!renderTarget.sharedHandle) {
			logger::info("RenderTargetManager::GetSharedTexture - Invalid shared handle for {}", magic_enum::enum_name(texture));
			return sharedTexture;
		}

		HRESULT hr;
		auto nativeD3D11Device = Renderer::GetSingleton()->GetNativeD3D11Device();
		if ((renderTarget.handle->getDesc().sharedResourceFlags & nvrhi::SharedResourceFlags::Shared_NTHandle) != 0)
			hr = nativeD3D11Device->OpenSharedResource1(renderTarget.sharedHandle, IID_PPV_ARGS(renderTarget.d3d11Texture.put()));
		else
			hr = nativeD3D11Device->OpenSharedResource(renderTarget.sharedHandle, IID_PPV_ARGS(renderTarget.d3d11Texture.put()));

		if (FAILED(hr)) {
			logger::info("RenderTargetManager::GetSharedTexture - Open shared resource failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
			return sharedTexture;
		}
	}

	sharedTexture.native = renderTarget.d3d12Resource.get();
	sharedTexture.shared = renderTarget.d3d11Texture.get();
	return sharedTexture;
}

SharedTexture RenderTargetManager::GetSharedTexture(Texture texture) {
	return GetSharedTexture(texture, Renderer::GetSingleton()->GetCompletedSlot());
}
