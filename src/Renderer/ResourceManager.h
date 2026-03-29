#pragma once

#include <PCH.h>

struct TransientTextureDesc
{
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t arraySize = 1;
	uint32_t mipLevels = 1;
	nvrhi::TextureDimension dimension = nvrhi::TextureDimension::Texture2D;
	nvrhi::Format format = nvrhi::Format::UNKNOWN;
	nvrhi::ResourceStates initialState = nvrhi::ResourceStates::Common;
	bool isUAV = false;
	bool isRenderTarget = false;
	bool isTypeless = false;
	bool keepInitialState = true;
	bool useClearValue = false;
	nvrhi::Color clearValue = nvrhi::Color(0.f);
	eastl::string debugName;
};

class ResourceManager
{
	static constexpr uint32_t kInvalidIndex = eastl::numeric_limits<uint32_t>::max();

	struct PhysicalTexture
	{
		nvrhi::TextureHandle texture;
		TransientTextureDesc desc;
		uint64_t lastUsedFrame = 0;
	};

	struct FrameAllocation
	{
		uint32_t physicalIndex = kInvalidIndex;
		uint32_t firstPass = 0;
		uint32_t lastPass = 0;
	};

	nvrhi::DeviceHandle m_Device;
	eastl::vector<PhysicalTexture> m_PhysicalTextures;
	eastl::vector<FrameAllocation> m_FrameAllocations;
	eastl::vector<uint32_t> m_ActiveScratchTextures;

	uint64_t m_CurrentFrameIndex = 0;
	uint32_t m_CurrentPassIndex = 0;

	static bool ArePassRangesOverlapping(uint32_t aFirst, uint32_t aLast, uint32_t bFirst, uint32_t bLast)
	{
		return !(aLast < bFirst || bLast < aFirst);
	}

	static bool AreTextureDescsCompatible(const TransientTextureDesc& lhs, const TransientTextureDesc& rhs)
	{
		return lhs.width == rhs.width &&
			lhs.height == rhs.height &&
			lhs.arraySize == rhs.arraySize &&
			lhs.mipLevels == rhs.mipLevels &&
			lhs.dimension == rhs.dimension &&
			lhs.format == rhs.format &&
			lhs.isUAV == rhs.isUAV &&
			lhs.isRenderTarget == rhs.isRenderTarget &&
			lhs.isTypeless == rhs.isTypeless &&
			lhs.useClearValue == rhs.useClearValue;
	}

	bool IsScratchTextureActive(uint32_t physicalIndex) const
	{
		return eastl::find(m_ActiveScratchTextures.begin(), m_ActiveScratchTextures.end(), physicalIndex) != m_ActiveScratchTextures.end();
	}

	bool CanAliasPhysicalTexture(uint32_t physicalIndex, const TransientTextureDesc& desc, uint32_t firstPass, uint32_t lastPass, bool requireInactiveScratch) const
	{
		const PhysicalTexture& physicalTexture = m_PhysicalTextures[physicalIndex];

		if (!physicalTexture.texture || !AreTextureDescsCompatible(physicalTexture.desc, desc))
			return false;

		if (requireInactiveScratch && IsScratchTextureActive(physicalIndex))
			return false;

		for (const FrameAllocation& allocation : m_FrameAllocations)
		{
			if (allocation.physicalIndex != physicalIndex)
				continue;

			if (ArePassRangesOverlapping(allocation.firstPass, allocation.lastPass, firstPass, lastPass))
				return false;
		}

		return true;
	}

	nvrhi::TextureDesc BuildTextureDesc(const TransientTextureDesc& desc) const
	{
		nvrhi::TextureDesc textureDesc;
		textureDesc.width = desc.width;
		textureDesc.height = desc.height;
		textureDesc.arraySize = desc.arraySize;
		textureDesc.mipLevels = desc.mipLevels;
		textureDesc.dimension = desc.dimension;
		textureDesc.format = desc.format;
		textureDesc.initialState = desc.initialState;
		textureDesc.isUAV = desc.isUAV;
		textureDesc.isRenderTarget = desc.isRenderTarget;
		textureDesc.isTypeless = desc.isTypeless;
		textureDesc.keepInitialState = desc.keepInitialState;
		textureDesc.useClearValue = desc.useClearValue;
		textureDesc.clearValue = desc.clearValue;
		textureDesc.debugName = desc.debugName.empty() ? "Transient Texture" : desc.debugName.c_str();
		return textureDesc;
	}

	nvrhi::TextureHandle AcquireTextureInternal(const TransientTextureDesc& desc, uint32_t firstPass, uint32_t lastPass, bool scratchTexture)
	{
		if (!m_Device)
		{
			logger::error("ResourceManager::AcquireTextureInternal - Device was not initialized");
			return nullptr;
		}

		if (desc.width == 0 || desc.height == 0 || desc.format == nvrhi::Format::UNKNOWN)
		{
			logger::error("ResourceManager::AcquireTextureInternal - Invalid transient texture descriptor");
			return nullptr;
		}

		if (lastPass < firstPass)
			lastPass = firstPass;

		uint32_t physicalIndex = kInvalidIndex;

		for (uint32_t i = 0; i < m_PhysicalTextures.size(); ++i)
		{
			if (CanAliasPhysicalTexture(i, desc, firstPass, lastPass, scratchTexture))
			{
				physicalIndex = i;
				break;
			}
		}

		if (physicalIndex == kInvalidIndex)
		{
			PhysicalTexture physicalTexture;
			physicalTexture.desc = desc;
			physicalTexture.texture = m_Device->createTexture(BuildTextureDesc(desc));
			physicalTexture.lastUsedFrame = m_CurrentFrameIndex;

			physicalIndex = static_cast<uint32_t>(m_PhysicalTextures.size());
			m_PhysicalTextures.push_back(physicalTexture);
		}
		else
		{
			m_PhysicalTextures[physicalIndex].lastUsedFrame = m_CurrentFrameIndex;
		}

		FrameAllocation allocation;
		allocation.physicalIndex = physicalIndex;
		allocation.firstPass = firstPass;
		allocation.lastPass = lastPass;
		m_FrameAllocations.push_back(allocation);

		if (scratchTexture)
			m_ActiveScratchTextures.push_back(physicalIndex);

		return m_PhysicalTextures[physicalIndex].texture;
	}

public:
	void SetDevice(nvrhi::DeviceHandle device)
	{
		m_Device = device;
	}

	void BeginFrame(uint64_t frameIndex)
	{
		m_CurrentFrameIndex = frameIndex;
		m_CurrentPassIndex = 0;
		m_FrameAllocations.clear();
		m_ActiveScratchTextures.clear();
	}

	void EndFrame()
	{
		m_ActiveScratchTextures.clear();
	}

	uint32_t BeginPass()
	{
		return ++m_CurrentPassIndex;
	}

	uint32_t GetCurrentPassIndex() const
	{
		return m_CurrentPassIndex;
	}

	nvrhi::TextureHandle AcquireTexture(const TransientTextureDesc& desc)
	{
		return AcquireTextureInternal(desc, m_CurrentPassIndex, m_CurrentPassIndex, false);
	}

	nvrhi::TextureHandle AcquireTexture(const TransientTextureDesc& desc, uint32_t lastUsePass)
	{
		const uint32_t clampedLastUsePass = eastl::max(lastUsePass, m_CurrentPassIndex);
		return AcquireTextureInternal(desc, m_CurrentPassIndex, clampedLastUsePass, false);
	}

	nvrhi::TextureHandle AcquireTextureForPassRange(const TransientTextureDesc& desc, uint32_t firstPass, uint32_t lastPass)
	{
		return AcquireTextureInternal(desc, firstPass, lastPass, false);
	}

	nvrhi::TextureHandle AcquireScratchTexture(const TransientTextureDesc& desc)
	{
		return AcquireTextureInternal(desc, m_CurrentPassIndex, m_CurrentPassIndex, true);
	}

	void ReleaseScratchTexture(const nvrhi::TextureHandle& texture)
	{
		if (!texture)
			return;

		for (auto it = m_ActiveScratchTextures.begin(); it != m_ActiveScratchTextures.end(); ++it)
		{
			if (m_PhysicalTextures[*it].texture == texture)
			{
				m_ActiveScratchTextures.erase(it);
				return;
			}
		}
	}
};
