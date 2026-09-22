#pragma once

#include <PCH.h>
#include "Framework/DescriptorTableManager.h"
#include "Types/BindlessTableManager.h"

struct TextureReference
{
	winrt::com_ptr<ID3D11Resource> sourceTexture;
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

	std::mutex m_TexturesMutex;

	eastl::unordered_map<IUnknown*, eastl::shared_ptr<TextureReference>> m_Textures;

	eastl::unique_ptr<BindlessTableManager> m_TextureDescriptors;
	eastl::unique_ptr<BindlessTableManager> m_CubemapDescriptors;

	TextureManager();
	uint64_t GetFakeDoubledVRAMUsage();
	eastl::shared_ptr<DescriptorHandle> GetDescriptor(RE::BSGraphics::Texture* texture, TextureType textureType = TextureType::Standard);
	void ReleaseTexture(RE::BSGraphics::Texture* texture);
	void ProcessPendingReleases(uint64_t completedFence, uint64_t lastSubmittedFence);

private:
	struct PendingRelease
	{
		eastl::unique_ptr<TextureReference> texture;
		uint64_t fence = UINT64_MAX;
	};

	struct ReleaseQueue
	{
		std::mutex mutex;
		eastl::vector<PendingRelease> textures;
	};

	eastl::shared_ptr<ReleaseQueue> m_ReleaseQueue = eastl::make_shared<ReleaseQueue>();
};

using TextureType = TextureManager::TextureType;
