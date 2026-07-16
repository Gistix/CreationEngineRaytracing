#pragma once

#include "RenderTexture.h"

#include "Types/SharedTexture.h"

#include "Constants.h"

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
		Accumulation,
		ReSTIRDI_Reservoir,
		Total
	};

	eastl::array<eastl::array<RenderTexture, static_cast<size_t>(Texture::Total)>, Constants::MAX_FRAMES_IN_FLIGHT> m_Textures;

	eastl::array<uint64_t, static_cast<size_t>(Texture::Total)> m_LastUse;

	nvrhi::ITexture* GetTexture(Texture texture, uint32_t slot);
	nvrhi::ITexture* GetTexture(Texture texture);

	SharedTexture GetSharedTexture(Texture texture, uint32_t slot);
	SharedTexture GetSharedTexture(Texture texture);
};

using RenderTarget = RenderTargetManager::Texture;