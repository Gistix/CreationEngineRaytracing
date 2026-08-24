#pragma once

#if defined(FALLOUT4)

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSShaderMaterial.h"
#include "RE/N/NiColor.h"
#include "RE/N/NiPlane.h"
#include "RE/N/NiPointer.h"
#include "RE/N/NiTexture.h"

namespace RE
{
	// CommonLibF4 does not currently expose the concrete material layouts. These
	// definitions mirror the FO4 1.10.163 layouts used by the material adapter.
	class BSLightingShaderMaterialEnvmap : public BSLightingShaderMaterialBase
	{
	public:
		NiPointer<NiTexture> envTexture;       // C0
		NiPointer<NiTexture> envMaskTexture;   // C8
		float                envMapScale;      // D0
		std::uint32_t        padD4;            // D4
	};

	class BSLightingShaderMaterialGlowmap : public BSLightingShaderMaterialBase
	{
	public:
		NiPointer<NiTexture> glowTexture;      // C0
	};

	class BSLightingShaderMaterialParallax : public BSLightingShaderMaterialBase
	{
	public:
		NiPointer<NiTexture> heightTexture;    // C0
	};

	class BSLightingShaderMaterialParallaxOcc : public BSLightingShaderMaterialBase
	{
	public:
		NiPointer<NiTexture> heightTexture;    // C0
		NiPointer<NiTexture> unkC8;            // C8
	};

	class BSLightingShaderMaterialFace : public BSLightingShaderMaterialBase
	{
	public:
		NiPointer<NiTexture> faceTexture;      // C0
	};

	class BSLightingShaderMaterialSkinTint : public BSLightingShaderMaterialBase
	{
	public:
		NiColor tintColor;                     // C0
	};

	class BSLightingShaderMaterialHairTint : public BSLightingShaderMaterialBase
	{
	public:
		NiColor tintColor;                     // C0
	};

	class BSLightingShaderMaterialEye : public BSLightingShaderMaterialBase
	{
	public:
		NiPointer<NiTexture> envTexture;       // C0
		NiPointer<NiTexture> envMaskTexture;   // C8
		float                unkD0[6];         // D0
		float                envMapScale;      // E8
	};

	class BSLightingShaderMaterialMultiLayerParallax : public BSLightingShaderMaterialBase
	{
	public:
		NiPointer<NiTexture> layerTexture;             // C0
		NiPointer<NiTexture> envTexture;               // C8
		NiPointer<NiTexture> envMaskTexture;           // D0
		float                parallaxLayerThickness;   // D8
		float                parallaxRefractionScale;  // DC
		float                parallaxInnerLayerUScale; // E0
		float                parallaxInnerLayerVScale; // E4
		float                envmapScale;              // E8
	};

	class BSLightingShaderMaterialLandscape : public BSLightingShaderMaterialBase
	{
	public:
		std::uint16_t        unkC0;                     // C0
		std::uint8_t         unkC2[0x4A];                // C2
		std::uint32_t        textureCount;              // 10C
		NiPointer<NiTexture> landscapeDiffuseTexture[3]; // 110
		NiPointer<NiTexture> landscapeNormalTexture[3];  // 128
		NiPointer<NiTexture> landscapeSpecularTexture[3]; // 140
		NiPointer<NiTexture> terrainOverlayTexture;     // 158
		NiPointer<NiTexture> terrainNoiseTexture;       // 160
		NiPointer<NiTexture> unk168;                    // 168
	};

	class BSLightingShaderMaterialLODLandscape : public BSLightingShaderMaterialBase
	{
	public:
		NiPointer<NiTexture> parentDiffuseTexture;     // C0
		NiPointer<NiTexture> parentNormalTexture;      // C8
		NiPointer<NiTexture> landscapeNoiseTexture;    // D0
		float                terrainTexOffsetX;         // D8
		float                terrainTexOffsetY;         // DC
		float                terrainTexFade;            // E0
	};

	class BSEffectShaderMaterial : public BSShaderMaterial
	{
	public:
		float                falloffStartAngle;        // 38
		float                falloffStopAngle;         // 3C
		float                falloffStartOpacity;      // 40
		float                falloffStopOpacity;       // 44
		NiColorA             baseColor;                // 48
		NiPointer<NiTexture> sourceTexture;            // 58
		NiPointer<NiTexture> greyscaleTexture;         // 60
		NiPointer<NiTexture> paletteTexture;           // 68
		NiPointer<NiTexture> normalTexture;            // 70
		NiPointer<NiTexture> refractionTexture;        // 78
		float                softFalloffDepth;         // 80
		float                baseColorScale;            // 84
	};

}

#endif
