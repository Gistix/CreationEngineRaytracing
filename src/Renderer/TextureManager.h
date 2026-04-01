#pragma once

struct TextureManager
{
	enum class Texture
	{
		ViewDepth,
		DiffuseRadiance,
		SpecularRadiance,
		Total
	};

	eastl::array<nvrhi::TextureHandle, static_cast<size_t>(Texture::Total)> m_Textures;

	nvrhi::ITexture* GetTexture(Texture texture);
};
