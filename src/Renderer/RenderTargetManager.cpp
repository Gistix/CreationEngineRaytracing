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

		if ((desc.sharedResourceFlags & nvrhi::SharedResourceFlags::Shared) == 0)
			renderTarget.handle = device->createTexture(desc);
		else {
			D3D12_RESOURCE_DESC nativeDesc = nvrhi::d3d12::convertTextureDesc(desc);

			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

			D3D12_RESOURCE_STATES state = nvrhi::d3d12::convertResourceStates(desc.initialState);

			D3D12_COMPATIBILITY_SHARED_FLAGS compatFlags = D3D12_COMPATIBILITY_SHARED_FLAG_NON_NT_HANDLE;

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

			auto hr = Renderer::GetCompatDevice()->CreateSharedResource(
				&heapProps, 
				D3D12_HEAP_FLAG_SHARED, 
				&nativeDesc, 
				state, 
				nullptr, 
				&flags11, 
				compatFlags, 
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

			hr = Renderer::GetCompatDevice()->ReflectSharedProperties(
				renderTarget.d3d12Resource.get(), 
				D3D12_REFLECT_SHARED_PROPERTY_NON_NT_SHARED_HANDLE, 
				&renderTarget.sharedHandle, sizeof(HANDLE));

			if (FAILED(hr)) {
				logger::info("RenderTargetManager::GetTexture - Reflect shared properties failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
				return nullptr;
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

	if (!renderTarget.d3d11Texture) {
		if (!renderTarget.sharedHandle) {
			logger::info("RenderTargetManager::GetSharedTexture - Invalid shared handle for {}", magic_enum::enum_name(texture));
			return sharedTexture;
		}

		auto hr = Renderer::GetSingleton()->GetNativeD3D11Device()->OpenSharedResource(renderTarget.sharedHandle, IID_PPV_ARGS(renderTarget.d3d11Texture.put()));

		if (FAILED(hr)) {
			logger::info("RenderTargetManager::GetSharedTexture - Open shared resource failed for {} with a hr of {:0X}", magic_enum::enum_name(texture), hr);
			return sharedTexture;
		}
	}

	sharedTexture.native = renderTarget.d3d12Resource.get();
	sharedTexture.shared = renderTarget.d3d11Texture.get();
	return sharedTexture;
}