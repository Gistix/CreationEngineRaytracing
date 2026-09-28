#pragma once

#include "RenderTexture.h"

#include "Types/SharedTexture.h"

struct RenderTargetManager
{
	enum class Texture
	{
		Main,
		ViewDepth,
		ClipDepth,
		FaceNormals,
		MotionVectors3D,
		DiffuseAlbedo,
		DiffuseRadiance,
		SpecularRadiance,
		DiffuseFactor,
		RRDiffuseAlbedo = DiffuseFactor,
		SpecularFactor,
		RRSpecularAlbedo = SpecularFactor,
		RRSpecularHitDist,
		DownscaledMotionVectors,
		DownscaledNormalRoughness,
		Accumulation,
		Total
	};

	eastl::array<RenderTexture, static_cast<size_t>(Texture::Total)> m_Textures;

	nvrhi::ITexture* GetTexture(Texture texture);

	SharedTexture GetSharedTexture(Texture texture);

	void CopySharedTextures(nvrhi::ICommandList* commandList);
};

using RenderTarget = RenderTargetManager::Texture;