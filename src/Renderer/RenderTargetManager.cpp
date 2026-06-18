#include "RenderTargetManager.h"
#include "Renderer.h"

nvrhi::ITexture* RenderTargetManager::GetTexture(Texture texture) {
	auto& renderTarget = m_Textures[static_cast<size_t>(texture)];

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
		desc.debugName = magic_enum::enum_name(texture);

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
			desc.format = nvrhi::Format::R32_FLOAT;
			break;
		case RenderTarget::ClipDepth:
			desc.format = nvrhi::Format::R32_FLOAT;
			desc.sharedResourceFlags = nvrhi::SharedResourceFlags::Shared;
			break;
		case RenderTarget::FaceNormals:
			desc.format = nvrhi::Format::R11G11B10_FLOAT;
			break;
		case RenderTarget::MotionVectors3D:
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.sharedResourceFlags = nvrhi::SharedResourceFlags::Shared;
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

		logger::debug("RenderTargetManager::GetTexture - Dimensions: [{}, {}], Format: {}, Shared: {} - {}", 
			desc.width, desc.height,
			magic_enum::enum_name(desc.format), 
			(desc.sharedResourceFlags & nvrhi::SharedResourceFlags::Shared) != nvrhi::SharedResourceFlags::None,
			desc.debugName);

		if ((desc.sharedResourceFlags & nvrhi::SharedResourceFlags::Shared) == nvrhi::SharedResourceFlags::None)
			renderTarget.handle = device->createTexture(desc);
		else {
			D3D12_RESOURCE_DESC nativeDesc = nvrhi::d3d12::convertTextureDesc(desc);

			D3D11_RESOURCE_FLAGS flags{};
			flags.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
			
			if (nativeDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
				flags.BindFlags |= D3D11_BIND_RENDER_TARGET;

			if (nativeDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
				flags.BindFlags |= D3D11_BIND_DEPTH_STENCIL;

			if (nativeDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
				flags.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

			if (!(nativeDesc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
				flags.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

			flags.CPUAccessFlags = 0;

			// Legacy shared textures have better format support, but require using compatibility device which may be unavailable (especially when using VKD3D)
			if (auto compatDevice = Renderer::GetCompatDevice()) {
				D3D12_HEAP_PROPERTIES heapProps = {};
				heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

				D3D12_RESOURCE_STATES state = nvrhi::d3d12::convertResourceStates(desc.initialState);

				auto hr = compatDevice->CreateSharedResource(
					&heapProps,
					D3D12_HEAP_FLAG_SHARED,
					&nativeDesc,
					state,
					nullptr,
					&flags,
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

				hr = Renderer::GetSingleton()->GetNativeD3D11Device()->OpenSharedResource(renderTarget.sharedHandle, IID_PPV_ARGS(renderTarget.d3d11Texture.put()));

				if (FAILED(hr)) {
					logger::info("RenderTargetManager::GetTexture - Open shared resource failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
					return nullptr;
				}
			}
			else {
				D3D11_TEXTURE2D_DESC d3d11Desc{};
				d3d11Desc.Width = static_cast<UINT>(nativeDesc.Width);
				d3d11Desc.Height = nativeDesc.Height;
				d3d11Desc.MipLevels = nativeDesc.MipLevels;
				d3d11Desc.ArraySize = nativeDesc.DepthOrArraySize;

				d3d11Desc.Format = nativeDesc.Format;

				d3d11Desc.SampleDesc.Count = nativeDesc.SampleDesc.Count;
				d3d11Desc.SampleDesc.Quality = nativeDesc.SampleDesc.Quality;

				d3d11Desc.Usage = D3D11_USAGE_DEFAULT;
				d3d11Desc.BindFlags = flags.BindFlags;
				d3d11Desc.CPUAccessFlags = flags.CPUAccessFlags;
				d3d11Desc.MiscFlags = flags.MiscFlags;

				auto nativeD3D12Device = Renderer::GetSingleton()->GetNativeD3D12Device();
				auto d3d11Device = Renderer::GetSingleton()->GetNativeD3D11Device();

				auto hr = d3d11Device->CreateTexture2D(&d3d11Desc, nullptr, renderTarget.d3d11Texture.put());
				if (FAILED(hr)) {
					logger::info("RenderTargetManager::GetTexture - D3D11 CreateTexture2D failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
					return nullptr;
				}

				// Get shared handle from D3D11 texture to enable D3D12 access
				winrt::com_ptr<IDXGIResource1> dxgiResource;
				hr = renderTarget.d3d11Texture->QueryInterface(IID_PPV_ARGS(dxgiResource.put()));
				if (FAILED(hr)) {
					logger::info("RenderTargetManager::GetTexture - D3D11 CreateTexture2D failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
					return nullptr;
				}

				// Get shared handle for legacy shared resources
				hr = dxgiResource->GetSharedHandle(&renderTarget.sharedHandle);
				if (FAILED(hr)) {
					logger::info("RenderTargetManager::GetTexture - GetSharedHandle failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
					return nullptr;
				}

				hr = nativeD3D12Device->OpenSharedHandle(renderTarget.sharedHandle, IID_PPV_ARGS(renderTarget.d3d12Resource.put()));
				if (FAILED(hr)) {
					logger::info("RenderTargetManager::GetTexture - GetSharedHandle failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
					return nullptr;
				}

				renderTarget.handle = device->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(renderTarget.d3d12Resource.get()), desc);
			}
		}		
	}

	return renderTarget.handle;
}

SharedTexture RenderTargetManager::GetSharedTexture(Texture texture) {
	SharedTexture sharedTexture;

	auto* dx12Texture = GetTexture(texture);
	if (!dx12Texture) {
		logger::info("RenderTargetManager::GetSharedTexture - Invalid texture for {}", magic_enum::enum_name(texture));
		return sharedTexture;
	}

	auto& renderTarget = m_Textures[static_cast<size_t>(texture)];
	sharedTexture.native = renderTarget.d3d12Resource.get();
	sharedTexture.shared = renderTarget.d3d11Texture.get();
	return sharedTexture;
}