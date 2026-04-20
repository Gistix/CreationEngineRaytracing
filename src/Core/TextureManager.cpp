#include "TextureManager.h"
#include "Core/D3D12Texture.h"
#include "Renderer.h"

TextureManager::TextureManager()
{
	auto device = Renderer::GetSingleton()->GetDevice();

	// Texture bindless descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_TEXTURES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::Texture_SRV(3).setSize(UINT_MAX)
		};

		m_TextureDescriptors = eastl::make_unique<BindlessTableManager>(device, bindlessLayoutDesc, true);
	}

	// Cubemap bindless descriptor table (space6)
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_CUBEMAPS_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::Texture_SRV(6).setSize(UINT_MAX)
		};

		m_CubemapDescriptors = eastl::make_unique<BindlessTableManager>(device, bindlessLayoutDesc, true);
	}

	m_MSNConverter = eastl::make_unique<Pipeline::MSNConverter>();
}

void TextureManager::ReleaseTexture(RE::BSGraphics::Texture* texture)
{
	if (!texture)
		return;

	IUnknown* key = nullptr;

	if (texture->pad24 == 1)
		key = reinterpret_cast<RE::BSGraphics::D3D12Texture*>(texture)->d3d12Texture;
	else
		key = texture->texture;

	m_Textures.erase(key);
	m_NormalMaps.erase(key);
}

eastl::shared_ptr<DescriptorHandle> TextureManager::GetDescriptor(RE::BSGraphics::Texture* texture, TextureType textureType)
{
	bool shareResource = true;

	ID3D11Resource* d3d11Resource = texture->texture;
	ID3D12Resource* d3d12Resource = nullptr;

	// Texure was already loaded on DX12
	if (texture->pad24 == 1) {
		shareResource = false;
		d3d12Resource = reinterpret_cast<RE::BSGraphics::D3D12Texture*>(texture)->d3d12Texture;
	}

	return GetDescriptor(d3d11Resource, d3d12Resource, textureType);
}

eastl::shared_ptr<DescriptorHandle> TextureManager::GetDescriptor(ID3D11Resource* d3d11Resource, ID3D12Resource* d3d12Resource, TextureType textureType)
{
	bool shareResource = d3d12Resource == nullptr;

	if (shareResource && !d3d11Resource)
		return nullptr;

	IUnknown* key = nullptr;

	if (shareResource)
		key = d3d11Resource;
	else
		key = d3d12Resource;

	if (textureType == TextureType::ModelSpaceNormalMap) {
		if (auto refIt = m_NormalMaps.find(key); refIt != m_NormalMaps.end())
			return refIt->second->descriptorHandle;
	}
	else {
		if (auto refIt = m_Textures.find(key); refIt != m_Textures.end())
			return refIt->second->descriptorHandle;
	}

	// Share texture from DX11 to DX12
	if (shareResource) {
		auto d3d11Texture = reinterpret_cast<ID3D11Texture2D*>(d3d11Resource);

		winrt::com_ptr<IDXGIResource> dxgiResource;
		HRESULT hr = d3d11Texture->QueryInterface(IID_PPV_ARGS(&dxgiResource));

		if (FAILED(hr)) {
			logger::error("{} - Failed to query interface.", __FUNCTION__);
			return nullptr;
		}

		HANDLE sharedHandle = nullptr;
		hr = dxgiResource->GetSharedHandle(&sharedHandle);

		if (FAILED(hr) || !sharedHandle) {
			D3D11_TEXTURE2D_DESC desc;
			d3d11Texture->GetDesc(&desc);

			logger::debug("TextureManager::GetDescriptor - Failed to get shared handle - [{}, {}] Format: {}", desc.Width, desc.Height, magic_enum::enum_name(desc.Format));
			return nullptr;
		}

		auto* d3d12Device = Renderer::GetSingleton()->GetNativeD3D12Device();

		hr = d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(&d3d12Resource));

		if (FAILED(hr)) {
			logger::error("TextureManager::GetDescriptor - Failed to open shared handle.");
			return nullptr;
		}

		if (!d3d12Resource) {
			logger::error("TextureManager::GetDescriptor - Failed to acquire DX12 texture.");
			return nullptr;
		}

		d3d12Resource->SetName(std::format(L"Shared Texture 0x{:08X}", reinterpret_cast<uintptr_t>(d3d11Resource)).c_str());
	}
	else if (!d3d12Resource) {
		logger::error("TextureManager::GetDescriptor - D3D12Resource is null");
		return nullptr;
	}

	// Create NVRHI handle for native texture
	D3D12_RESOURCE_DESC nativeTexDesc = d3d12Resource->GetDesc();

	auto formatIt = Renderer::GetFormatMapping().find(nativeTexDesc.Format);

	if (formatIt == Renderer::GetFormatMapping().end()) {
		logger::error("TextureManager::GetDescriptor - Unmapped format {}", magic_enum::enum_name(nativeTexDesc.Format));
		return nullptr;
	}

	auto& textureDesc = nvrhi::TextureDesc()
		.setWidth(static_cast<uint32_t>(nativeTexDesc.Width))
		.setHeight(nativeTexDesc.Height)
		.setFormat(formatIt->second)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName("Shared Texture [?]");

	auto textureHandle = Renderer::GetSingleton()->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(d3d12Resource), textureDesc);

	if (textureType == TextureType::ModelSpaceNormalMap) {
		auto [it, emplaced] = m_NormalMaps.emplace(d3d11Resource, nullptr);

		if (!emplaced) {
			logger::warn("TextureManager::GetDescriptor - NormalMap emplace failed.");
			return nullptr;
		}

		auto device = Renderer::GetSingleton()->GetDevice();

		// The converted normal map
		auto normalMapRT = device->createTexture(
			nvrhi::TextureDesc()
			.setWidth(textureDesc.width)
			.setHeight(textureDesc.height)
			.setFormat(nvrhi::Format::R10G10B10A2_UNORM)
			.setInitialState(nvrhi::ResourceStates::ShaderResource)
			.setKeepInitialState(true)
			.setIsRenderTarget(true)
			.setDebugName("Converted MSN Texture"));

		it->second = eastl::make_unique<MSNReference>(normalMapRT, m_TextureDescriptors->m_DescriptorTable.get());
		it->second->sourceTexture = textureHandle;

		m_MSNConverter->Allocate(it->second->descriptorHandle->Get(), d3d11Resource);
	}
	else {
		// NVRHI now owns the texture
		if (shareResource)
			d3d12Resource->Release();

		auto [it, emplaced] = m_Textures.try_emplace(key, nullptr);

		if (!emplaced) {
			logger::error("TextureManager::GetDescriptor - TextureReference emplace failed.");
			return nullptr;
		}

		it->second = eastl::make_unique<TextureReference>(textureHandle, m_TextureDescriptors->m_DescriptorTable.get());
		return it->second->descriptorHandle;
	}

	return nullptr;
}
