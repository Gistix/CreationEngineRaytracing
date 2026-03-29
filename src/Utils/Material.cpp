#pragma once

#include "Material.h"

namespace Util
{
#if defined(SKYRIM)
	namespace Material::Skyrim
	{
		float ShininessToRoughness(float shininess)
		{
			// make sure shininess within valid range (0 - 1023), otherwise set to 1.0f
			if (shininess <= 0.0f || shininess > 1023.0f) {
				return 1.0f;
			}
			return std::pow(2.0f / (shininess + 2.0f), 0.25f);
		}

		stl::enumeration<PBRShaderFlags, uint32_t> GetPBRShaderFlags(const BSLightingShaderMaterialPBR* pbrMaterial)
		{
			auto* graphicsState = RE::BSGraphics::State::GetSingleton();

			stl::enumeration<PBRShaderFlags, uint32_t> pbrFlags;

			if (pbrMaterial->pbrFlags.any(PBRFlags::TwoLayer)) {
				pbrFlags.set(PBRShaderFlags::TwoLayer);
				if (pbrMaterial->pbrFlags.any(PBRFlags::InterlayerParallax)) {
					pbrFlags.set(PBRShaderFlags::InterlayerParallax);
				}
				if (pbrMaterial->pbrFlags.any(PBRFlags::CoatNormal)) {
					pbrFlags.set(PBRShaderFlags::CoatNormal);
				}
				if (pbrMaterial->pbrFlags.any(PBRFlags::ColoredCoat)) {
					pbrFlags.set(PBRShaderFlags::ColoredCoat);
				}
			}
			else if (pbrMaterial->pbrFlags.any(PBRFlags::HairMarschner)) {
				pbrFlags.set(PBRShaderFlags::HairMarschner);
			}
			else {
				if (pbrMaterial->pbrFlags.any(PBRFlags::Subsurface)) {
					pbrFlags.set(PBRShaderFlags::Subsurface);
				}
				if (pbrMaterial->pbrFlags.any(PBRFlags::Fuzz)) {
					pbrFlags.set(PBRShaderFlags::Fuzz);
				}
				else {
					if (pbrMaterial->GetGlintParameters().enabled) {
						pbrFlags.set(PBRShaderFlags::Glint);
					}

					// This is slimmed down because we don't have access to lightingFlags
					if (pbrMaterial->GetProjectedMaterialGlintParameters().enabled) {
						pbrFlags.set(PBRShaderFlags::ProjectedGlint);
					}
				}
			}

			const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != graphicsState->GetRuntimeData().defaultTextureBlack;
			if (hasEmissive) {
				pbrFlags.set(PBRShaderFlags::HasEmissive);
			}

			const bool hasDisplacement = pbrMaterial->displacementTexture != nullptr && pbrMaterial->displacementTexture != graphicsState->GetRuntimeData().defaultTextureBlack;
			if (hasDisplacement) {
				pbrFlags.set(PBRShaderFlags::HasDisplacement);
			}

			const bool hasFeaturesTexture0 = pbrMaterial->featuresTexture0 != nullptr && pbrMaterial->featuresTexture0 != graphicsState->GetRuntimeData().defaultTextureWhite;
			if (hasFeaturesTexture0) {
				pbrFlags.set(PBRShaderFlags::HasFeaturesTexture0);
			}

			const bool hasFeaturesTexture1 = pbrMaterial->featuresTexture1 != nullptr && pbrMaterial->featuresTexture1 != graphicsState->GetRuntimeData().defaultTextureWhite;
			if (hasFeaturesTexture1) {
				pbrFlags.set(PBRShaderFlags::HasFeaturesTexture1);
			}

			return pbrFlags;
		}
	}
#elif defined(FALLOUT4)
#endif
}