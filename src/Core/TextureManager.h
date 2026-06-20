#pragma once

#include <PCH.h>
#include "Framework/DescriptorTableManager.h"
#include "Types/BindlessTableManager.h"
#include "Pipeline/MSNConverter.h"

#define NO_DX12RESOURCE 0
#define NATIVE_DX12RESOURCE 1

struct TextureReference
{
	nvrhi::TextureHandle texture;
	eastl::shared_ptr<DescriptorHandle> descriptorHandle;
	uint64_t size;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t mipLevels = 1;
	nvrhi::Format format = nvrhi::Format::UNKNOWN;
	uint32_t residentMipOffset = 0;

	TextureReference(nvrhi::TextureHandle texture, DescriptorTableManager* descriptorTableManager, uint32_t residentMipOffset = 0);

	virtual ~TextureReference() = default;
};

struct MSNReference : TextureReference
{
	nvrhi::TextureHandle sourceTexture; // Original MSN texture
	bool converted = false;

	MSNReference(nvrhi::TextureHandle texture, nvrhi::TextureHandle sourceTexture, DescriptorTableManager* manager)
		: TextureReference(texture, manager), sourceTexture(sourceTexture) {
	}

	virtual ~MSNReference() = default;
};

	struct TextureManager
	{
		enum class TextureType
		{
			Standard,
			ModelSpaceNormalMap,
			CubeMap
		};

		eastl::unordered_map<IUnknown*, eastl::unique_ptr<TextureReference>> m_Textures;
		eastl::unordered_map<IUnknown*, eastl::unique_ptr<MSNReference>> m_NormalMaps;

		eastl::unique_ptr<BindlessTableManager> m_TextureDescriptors;
		eastl::unique_ptr<BindlessTableManager> m_CubemapDescriptors;

		eastl::unique_ptr<Pipeline::MSNConverter> m_MSNConverter;

		TextureManager();
		static void RegisterResidentMipOffset(IUnknown* resource, uint32_t mipOffset);
		uint64_t GetFakeDoubledVRAMUsage();
		void LogMemoryStats();
		eastl::shared_ptr<DescriptorHandle> GetDescriptor(RE::BSGraphics::Texture* texture, TextureType textureType = TextureType::Standard);
		eastl::shared_ptr<DescriptorHandle> GetDescriptor(ID3D11Resource* d3d11Resource, ID3D12Resource* d3d12Resource = nullptr, TextureType textureType = TextureType::Standard);
		void ReleaseTexture(RE::BSGraphics::Texture* texture);
	};

using TextureType = TextureManager::TextureType;
