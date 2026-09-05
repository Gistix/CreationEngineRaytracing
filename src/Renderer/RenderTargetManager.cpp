#include "RenderTargetManager.h"
#include "Renderer.h"
#include "Scene.h"

nvrhi::ITexture* RenderTargetManager::GetTexture(Texture texture, uint32_t slot) {
	auto& renderTarget = m_Textures[slot][static_cast<size_t>(texture)];

	if (!renderTarget.handle) {
		auto* renderer = Renderer::GetSingleton();
		auto device = renderer->GetDevice();

		auto resolution = renderer->GetResolution();
		nvrhi::TextureDesc desc{};

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
			break;
		case RenderTarget::DiffuseRadiance:
		case RenderTarget::SpecularRadiance:
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			break;
		case RenderTarget::DiffuseFactor: // RRDiffuseAlbedo
		case RenderTarget::SpecularFactor: // RRSpecularAlbedo
			desc.format = nvrhi::Format::R11G11B10_FLOAT;
			break;
		case RenderTarget::RRSpecularHitDist:
			desc.format = nvrhi::Format::R32_FLOAT;
			break;
		case RenderTarget::DownscaledMotionVectors:
			desc.format = nvrhi::Format::RG16_FLOAT;
			break;
		case RenderTarget::DownscaledNormalRoughness:
			desc.format = nvrhi::Format::RGBA16_SNORM;
			break;
		default:
			break;
		}

		std::string debugName = std::format("{}_{}", magic_enum::enum_name(texture), slot);
		desc.debugName = debugName.c_str();

		logger::debug("RenderTargetManager::GetTexture - Slot: {}, Dimensions: [{}, {}], Format: {} - {}", 
			slot, desc.width, desc.height,
			magic_enum::enum_name(desc.format), 
			desc.debugName);

		renderTarget.handle = device->createTexture(desc);
	}

	return renderTarget.handle;
}

nvrhi::ITexture* RenderTargetManager::GetTexture(Texture texture) {
	return GetTexture(texture, Renderer::GetSingleton()->GetCurrentSlot());
}

SharedTexture RenderTargetManager::GetSharedTexture(Texture texture, uint32_t slot) {
	SharedTexture sharedTexture;

	auto* internalTexture = GetTexture(texture, slot);
	if (!internalTexture) {
		logger::info("RenderTargetManager::GetSharedTexture - Invalid texture for {}", magic_enum::enum_name(texture));
		return sharedTexture;
	}

	auto& renderTarget = m_Textures[slot][static_cast<size_t>(texture)];

	if (!renderTarget.d3d11Texture) {
		auto* renderer = Renderer::GetSingleton();
		auto* nativeD3D11Device = renderer->GetNativeD3D11Device();
		auto* nativeD3D12Device = renderer->GetNativeD3D12Device();

		if (!nativeD3D11Device || !nativeD3D12Device) {
			logger::error("RenderTargetManager::GetSharedTexture - D3D11 or D3D12 device is null");
			return sharedTexture;
		}

		const auto& internalDesc = internalTexture->getDesc();
		DXGI_FORMAT nativeFormat = nvrhi::d3d12::convertFormat(internalDesc.format);

		D3D11_TEXTURE2D_DESC desc11{};
		desc11.Width = internalDesc.width;
		desc11.Height = internalDesc.height;
		desc11.MipLevels = 1;
		desc11.ArraySize = 1;
		desc11.Format = nativeFormat;
		desc11.SampleDesc.Count = 1;
		desc11.SampleDesc.Quality = 0;
		desc11.Usage = D3D11_USAGE_DEFAULT;
		desc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc11.CPUAccessFlags = 0;
		desc11.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

		HRESULT hr = nativeD3D11Device->CreateTexture2D(&desc11, nullptr, renderTarget.d3d11Texture.put());
		if (FAILED(hr)) {
			// If RTV is not supported for this format, retry with only SHADER_RESOURCE
			desc11.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			hr = nativeD3D11Device->CreateTexture2D(&desc11, nullptr, renderTarget.d3d11Texture.put());
			if (FAILED(hr)) {
				logger::error("RenderTargetManager::GetSharedTexture - CreateTexture2D failed for {} with hr 0x{:08X}", magic_enum::enum_name(texture), static_cast<uint32_t>(hr));
				return sharedTexture;
			}
		}

		winrt::com_ptr<IDXGIResource> dxgiResource;
		hr = renderTarget.d3d11Texture->QueryInterface(IID_PPV_ARGS(dxgiResource.put()));
		if (FAILED(hr)) {
			logger::error("RenderTargetManager::GetSharedTexture - QueryInterface IDXGIResource failed for {} with hr 0x{:08X}", magic_enum::enum_name(texture), static_cast<uint32_t>(hr));
			return sharedTexture;
		}

		HANDLE sharedHandle = nullptr;
		hr = dxgiResource->GetSharedHandle(&sharedHandle);
		if (FAILED(hr) || !sharedHandle) {
			logger::error("RenderTargetManager::GetSharedTexture - GetSharedHandle failed for {} with hr 0x{:08X}", magic_enum::enum_name(texture), static_cast<uint32_t>(hr));
			return sharedTexture;
		}

		hr = nativeD3D12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(renderTarget.d3d12Resource.put()));
		if (FAILED(hr) || !renderTarget.d3d12Resource) {
			logger::error("RenderTargetManager::GetSharedTexture - OpenSharedHandle failed for {} with hr 0x{:08X}", magic_enum::enum_name(texture), static_cast<uint32_t>(hr));
			return sharedTexture;
		}

		std::string sharedDebugName = std::format("{}_{}_D3D11Shared", magic_enum::enum_name(texture), slot);
		renderTarget.sharedD3D12Handle = renderer->CreateHandleForNativeTexture(renderTarget.d3d12Resource.get(), sharedDebugName.c_str(), internalDesc.format, nvrhi::ResourceStates::Common);
	}

	sharedTexture.native = renderTarget.d3d12Resource.get();
	sharedTexture.shared = renderTarget.d3d11Texture.get();
	return sharedTexture;
}

void RenderTargetManager::CopySharedTextures(nvrhi::ICommandList* commandList, uint32_t slot) {
	auto currentMode = Scene::GetSingleton()->m_Settings.GeneralSettings.Mode;

	const bool gi = (currentMode == Mode::GlobalIllumination);
	const bool pt = (currentMode == Mode::PathTracing);

	if (!gi && !pt)
		return;

	auto copyTexture = [&](Texture texture){
		auto& rt = m_Textures[slot][static_cast<size_t>(texture)];

		if (rt.sharedD3D12Handle && rt.handle)
			commandList->copyTexture(rt.sharedD3D12Handle, nvrhi::TextureSlice(), rt.handle, nvrhi::TextureSlice());
	};

	if (gi || pt)
		copyTexture(Texture::Main);

	if (pt) {
		copyTexture(Texture::ViewDepth);
		copyTexture(Texture::MotionVectors3D);
	}
}