#pragma once

struct TextureManager
{
	enum class Texture
	{
		ViewDepth,
		ClipDepth,
		MotionVectors3D,
		DiffuseRadiance,
		SpecularRadiance,
		Total
	};

	eastl::array<nvrhi::TextureHandle, static_cast<size_t>(Texture::Total)> m_Textures;

	eastl::array<uint64_t, static_cast<size_t>(Texture::Total)> m_LastUse;

	nvrhi::ITexture* GetTexture(Texture texture);
};
