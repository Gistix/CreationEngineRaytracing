#pragma once

#include "RenderTexture.h"

#include "Types/SharedTexture.h"

struct RenderTargetManager
{
	enum class Texture
	{
		ViewDepth,
		ClipDepth,
		FaceNormals,
		MotionVectors3D,
		PathTracingDirect,
		DiffuseAlbedo,
		DiffuseRadiance,
		SpecularRadiance,
		DiffuseFactor,
		SpecularFactor,
		RRSpecularAlbedo = SpecularFactor,
		RRSpecularHitDist,
		Total
	};

	eastl::array<RenderTexture, static_cast<size_t>(Texture::Total)> m_Textures;

	eastl::array<uint64_t, static_cast<size_t>(Texture::Total)> m_LastUse;

	nvrhi::ITexture* GetTexture(Texture texture);

	SharedTexture GetSharedTexture(Texture texture);
};

using RenderTarget = RenderTargetManager::Texture;