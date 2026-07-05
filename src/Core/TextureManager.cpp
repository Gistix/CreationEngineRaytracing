#include "TextureManager.h"
#include "Core/D3D12Texture.h"
#include "Renderer.h"

namespace
{
	std::mutex g_ResidentMipOffsetsMutex;
	eastl::unordered_map<IUnknown*, uint32_t> g_ResidentMipOffsets;
}

TextureReference::TextureReference(nvrhi::TextureHandle texture, DescriptorTableManager* descriptorTableManager, uint32_t residentMipOffset) :
	texture(texture), residentMipOffset(residentMipOffset)
{
	descriptorHandle = eastl::make_shared<DescriptorHandle>(descriptorTableManager->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, texture)));
	size = Renderer::GetSingleton()->GetDevice()->getTextureMemoryRequirements(texture).size;

	const auto& desc = texture->getDesc();
	width = desc.width;
	height = desc.height;
	mipLevels = desc.mipLevels;
	format = desc.format;
}

void TextureManager::RegisterResidentMipOffset(IUnknown* resource, uint32_t mipOffset)
{
	if (!resource || mipOffset == 0)
		return;

	std::scoped_lock lock(g_ResidentMipOffsetsMutex);
	g_ResidentMipOffsets[resource] = mipOffset;
}

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
			nvrhi::BindingLayoutItem::Texture_SRV(4).setSize(UINT_MAX)
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
			nvrhi::BindingLayoutItem::Texture_SRV(7).setSize(UINT_MAX)
		};

		m_CubemapDescriptors = eastl::make_unique<BindlessTableManager>(device, bindlessLayoutDesc, true);
	}
}

uint64_t TextureManager::GetFakeDoubledVRAMUsage()
{
	uint64_t vramUsage = 0;

	for (const auto& [key, texture]: m_Textures)
	{
		if (texture)
			vramUsage += texture->size;
	}

	return vramUsage;
}

void TextureManager::LogMemoryStats()
{
	uint64_t standardBytes = 0;
	uint32_t streamedTextures = 0;
	uint64_t streamedBytes = 0;

	for (const auto& [key, texture] : m_Textures)
	{
		if (!texture)
			continue;

		standardBytes += texture->size;

		if (texture->residentMipOffset > 0) {
			streamedTextures++;
			streamedBytes += texture->size;
		}
	}

	logger::info(
		"TextureManager - RT texture memory: total={} MiB, standard={} MiB ({} textures), streamed={} MiB ({} textures)",
		standardBytes / (1024 * 1024),
		standardBytes / (1024 * 1024),
		m_Textures.size(),
		streamedBytes / (1024 * 1024),
		streamedTextures);
}

void TextureManager::ReleaseTexture(RE::BSGraphics::Texture* texture)
{
	if (!texture)
		return;

	IUnknown* key = nullptr;

#if defined(SKYRIM)
	if (texture->pad24 == NATIVE_DX12RESOURCE)
		key = reinterpret_cast<RE::BSGraphics::D3D12Texture*>(texture)->d3d12Texture;
	else
#endif
		key = texture->texture;

	m_Textures.erase(key);
}

eastl::shared_ptr<DescriptorHandle> TextureManager::GetDescriptor(RE::BSGraphics::Texture* texture, TextureType textureType)
{
	ID3D11Resource* d3d11Resource = texture->texture;
	ID3D12Resource* d3d12Resource = nullptr;

#if defined(SKYRIM)
	// Texure was already loaded on DX12
	if (texture->pad24 == NATIVE_DX12RESOURCE) {
		d3d12Resource = reinterpret_cast<RE::BSGraphics::D3D12Texture*>(texture)->d3d12Texture;
	}
#endif

	return GetDescriptor(d3d11Resource, d3d12Resource, textureType);
}

eastl::shared_ptr<DescriptorHandle> TextureManager::GetDescriptor(ID3D11Resource* d3d11Resource, ID3D12Resource* d3d12Resource, TextureType textureType)
{
	if (textureType == TextureType::CubeMap)
		return nullptr;

	// If d3d12Resource is null we need to get the texture handle from dx11
	bool shareResource = d3d12Resource == nullptr;
	if (shareResource && !d3d11Resource)
		return nullptr;

	IUnknown* key = nullptr;

	if (shareResource)
		key = d3d11Resource;
	else
		key = d3d12Resource;

	uint32_t residentMipOffset = 0;
	{
		std::scoped_lock lock(g_ResidentMipOffsetsMutex);
		if (auto it = g_ResidentMipOffsets.find(key); it != g_ResidentMipOffsets.end()) {
			residentMipOffset = it->second;
			g_ResidentMipOffsets.erase(it);
		}
	}

	if (textureType == TextureType::Standard) {
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

	auto format = Renderer::GetFormat(nativeTexDesc.Format);
	if (format == nvrhi::Format::UNKNOWN) {
		logger::error("TextureManager::GetDescriptor - Unmapped format {}", magic_enum::enum_name(nativeTexDesc.Format));
		return nullptr;
	}

	auto& textureDesc = nvrhi::TextureDesc()
		.setWidth(static_cast<uint32_t>(nativeTexDesc.Width))
		.setHeight(nativeTexDesc.Height)
		.setMipLevels(nativeTexDesc.MipLevels)
		.setFormat(format)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName("Shared Texture [?]");

	auto textureHandle = Renderer::GetSingleton()->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(d3d12Resource), textureDesc);

	auto [it, emplaced] = m_Textures.try_emplace(key, nullptr);

	if (!emplaced) {
		logger::error("TextureManager::GetDescriptor - TextureReference emplace failed.");
		return nullptr;
	}

	it->second = eastl::make_unique<TextureReference>(textureHandle, m_TextureDescriptors->m_DescriptorTable.get(), residentMipOffset);
	return it->second->descriptorHandle;

	return nullptr;
}
