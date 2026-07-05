#pragma once

#include <PCH.h>
#include "Framework/DescriptorTableManager.h"
#include "Types/BindlessTableManager.h"

#define NO_DX12RESOURCE 0
#define NATIVE_DX12RESOURCE 1

struct TextureReference
{
	nvrhi::TextureHandle texture;
	eastl::shared_ptr<DescriptorHandle> descriptorHandle;
	uint64_t size;

	TextureReference(nvrhi::TextureHandle texture, DescriptorTableManager* descriptorTableManager);

	virtual ~TextureReference() = default;
};

struct TextureManager
{
	enum class TextureType
	{
		Standard,
		CubeMap
	};

	eastl::unordered_map<IUnknown*, eastl::unique_ptr<TextureReference>> m_Textures;

	eastl::unique_ptr<BindlessTableManager> m_TextureDescriptors;
	eastl::unique_ptr<BindlessTableManager> m_CubemapDescriptors;

	TextureManager();
	uint64_t GetFakeDoubledVRAMUsage();
	eastl::shared_ptr<DescriptorHandle> GetDescriptor(RE::BSGraphics::Texture* texture, TextureType textureType = TextureType::Standard);
	eastl::shared_ptr<DescriptorHandle> GetDescriptor(ID3D11Resource* d3d11Resource, ID3D12Resource* d3d12Resource = nullptr, TextureType textureType = TextureType::Standard);
	void ReleaseTexture(RE::BSGraphics::Texture* texture);
};

using TextureType = TextureManager::TextureType;