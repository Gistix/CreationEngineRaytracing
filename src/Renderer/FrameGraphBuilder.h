#pragma once

#include <PCH.h>

#include "Renderer/ResourceManager.h"

class Renderer;

enum class FrameGraphTextureId : uint32_t
{
	NrdDiffuseRadianceHitDistance = 0,
	NrdSpecularRadianceHitDistance,
	Count
};

class FrameGraphBuilder
{
	struct TextureEntry
	{
		bool hasDescriptor = false;
		TransientTextureDesc desc;
		uint32_t firstPass = 0;
		uint32_t lastPass = 0;
	};

	Renderer* m_Renderer = nullptr;
	eastl::array<TextureEntry, static_cast<size_t>(FrameGraphTextureId::Count)> m_TextureEntries = {};
	uint32_t m_CurrentPassIndex = 0;

	TextureEntry& GetEntry(FrameGraphTextureId id)
	{
		return m_TextureEntries[static_cast<size_t>(id)];
	}

	const TextureEntry& GetEntry(FrameGraphTextureId id) const
	{
		return m_TextureEntries[static_cast<size_t>(id)];
	}

public:
	explicit FrameGraphBuilder(Renderer* renderer)
		: m_Renderer(renderer)
	{
	}

	void BeginPass()
	{
		++m_CurrentPassIndex;
	}

	void ReadTexture(FrameGraphTextureId id)
	{
		TextureEntry& entry = GetEntry(id);
		if (entry.firstPass == 0)
			entry.firstPass = m_CurrentPassIndex;

		entry.lastPass = eastl::max(entry.lastPass, m_CurrentPassIndex);
	}

	void WriteTexture(FrameGraphTextureId id, const TransientTextureDesc& desc)
	{
		TextureEntry& entry = GetEntry(id);
		entry.hasDescriptor = true;
		entry.desc = desc;

		if (entry.firstPass == 0)
			entry.firstPass = m_CurrentPassIndex;

		entry.lastPass = eastl::max(entry.lastPass, m_CurrentPassIndex);
	}

	void Compile();
};
