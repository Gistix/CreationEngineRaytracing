#pragma once

#if defined(FALLOUT4)

#include "RE/B/BSFixedString.h"
#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSShaderMaterial.h"
#include "RE/B/BSWaterShaderMaterial.h"
#include "RE/N/NiColor.h"
#include "RE/N/NiPlane.h"
#include "RE/N/NiPoint3.h"
#include "RE/N/NiPointer.h"
#include "RE/N/NiTexture.h"

namespace RE
{
	class __declspec(novtable) BSLightingShaderMaterialEnvmap : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialEnvmap };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialEnvmap };
		static constexpr auto FEATURE{ Feature::kEnvmap };

		// members
		NiPointer<NiTexture> envTexture;      // C0
		NiPointer<NiTexture> envMaskTexture;  // C8
		float                envMapScale;     // D0
		std::uint8_t         padD4;           // D4
		std::uint8_t         padD5;           // D5
		std::uint16_t        padD6;           // D6
	};
	static_assert(sizeof(BSLightingShaderMaterialEnvmap) == 0xD8);

	class __declspec(novtable) BSLightingShaderMaterialGlowmap : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialGlowmap };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialGlowmap };
		static constexpr auto FEATURE{ Feature::kGlowmap };

		// members
		NiPointer<NiTexture> glowTexture;  // C0
	};
	static_assert(sizeof(BSLightingShaderMaterialGlowmap) == 0xC8);

	class __declspec(novtable) BSLightingShaderMaterialParallax : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialParallax };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialParallax };
		static constexpr auto FEATURE{ Feature::kParallax };

		// members
		NiPointer<NiTexture> heightTexture;  // C0
	};
	static_assert(sizeof(BSLightingShaderMaterialParallax) == 0xC8);

	class __declspec(novtable) BSLightingShaderMaterialParallaxOcc : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialParallaxOcc };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialParallaxOcc };
		static constexpr auto FEATURE{ Feature::kParallaxOcc };

		// members
		NiPointer<NiTexture> heightTexture;                    // C0
		float                parallaxOccMaterialPCMul;         // C8
		float                parallaxOccShadowStepMultiplier;  // CC
	};
	static_assert(sizeof(BSLightingShaderMaterialParallaxOcc) == 0xD0);

	class __declspec(novtable) BSLightingShaderMaterialDismemberment : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialDismemberment };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialDismemberment };
		static constexpr auto FEATURE{ Feature::kDismemberment };

		// members
		NiPointer<NiTexture> dismemberTexture;  // C0
	};
	static_assert(sizeof(BSLightingShaderMaterialDismemberment) == 0xC8);

	class __declspec(novtable) BSLightingShaderMaterialFace : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialFace };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialFace };
		static constexpr auto FEATURE{ Feature::kFace };

		// members
		NiPointer<NiTexture> faceTexture;  // C0
	};
	static_assert(sizeof(BSLightingShaderMaterialFace) == 0xC8);

	class __declspec(novtable) BSLightingShaderMaterialSkinTint : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialSkinTint };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialSkinTint };
		static constexpr auto FEATURE{ Feature::kSkinTint };

		// members
		NiColorA tintColor;  // C0
	};
	static_assert(sizeof(BSLightingShaderMaterialSkinTint) == 0xD0);

	class __declspec(novtable) BSLightingShaderMaterialHairTint : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialHairTint };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialHairTint };
		static constexpr auto FEATURE{ Feature::kHairTint };

		// members
		NiColor       tintColor;  // C0
		std::uint32_t padCC;      // CC
	};
	static_assert(sizeof(BSLightingShaderMaterialHairTint) == 0xD0);

	class __declspec(novtable) BSLightingShaderMaterialEye : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialEye };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialEye };
		static constexpr auto FEATURE{ Feature::kEye };

		// members
		NiPointer<NiTexture> envTexture;      // C0
		NiPointer<NiTexture> envMaskTexture;  // C8
		NiPoint3             eyeCenter[2];    // D0 (left eye = [0], right eye = [1])
		float                envMapScale;     // E8
		std::uint32_t        padEC;           // EC
	};
	static_assert(sizeof(BSLightingShaderMaterialEye) == 0xF0);

	class __declspec(novtable) BSLightingShaderMaterialSnow : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialSnow };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialSnow };
		static constexpr auto FEATURE{ Feature::kSnow };

		// members
		NiColorA snowColor;  // C0
	};
	static_assert(sizeof(BSLightingShaderMaterialSnow) == 0xD0);

	class __declspec(novtable) BSLightingShaderMaterialMultiLayerParallax : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialMultiLayerParallax };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialMultiLayerParallax };
		static constexpr auto FEATURE{ Feature::kMultiLayerParallax };

		// members
		NiPointer<NiTexture> layerTexture;              // C0
		NiPointer<NiTexture> envTexture;                // C8
		NiPointer<NiTexture> envMaskTexture;            // D0
		float                parallaxLayerThickness;    // D8
		float                parallaxRefractionScale;   // DC
		float                parallaxInnerLayerUScale;  // E0
		float                parallaxInnerLayerVScale;  // E4
		float                envmapScale;               // E8
		std::uint32_t        padEC;                     // EC
	};
	static_assert(sizeof(BSLightingShaderMaterialMultiLayerParallax) == 0xF0);

	class __declspec(novtable) BSLightingShaderMaterialLandscape : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialLandscape };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialLandscape };
		static constexpr auto FEATURE{ Feature::kLODLandscapeBlend };

		// members
		std::uint16_t        unkC0;                       // C0
		std::uint8_t         unkC2;                       // C2
		std::uint8_t         padC3;                       // C3
		float                landscapeParams[6][3];       // C4
		std::uint32_t        textureCount;                // 10C
		NiPointer<NiTexture> landscapeDiffuseTexture[3];  // 110
		NiPointer<NiTexture> landscapeNormalTexture[3];   // 128
		NiPointer<NiTexture> landscapeSpecularTexture[3]; // 140
		NiPointer<NiTexture> terrainOverlayTexture;       // 158
		NiPointer<NiTexture> terrainNoiseTexture;         // 160
		NiPointer<NiTexture> unk168;                      // 168
		NiColorA             terrainOverlayColor;         // 170
		float                unk180[2];                   // 180
		std::uint32_t        unk188;                      // 188
		std::uint32_t        pad18C;                      // 18C
	};
	static_assert(sizeof(BSLightingShaderMaterialLandscape) == 0x190);

	class __declspec(novtable) BSLightingShaderMaterialLODLandscape : public BSLightingShaderMaterialBase
	{
	public:
		static constexpr auto RTTI{ RTTI::BSLightingShaderMaterialLODLandscape };
		static constexpr auto VTABLE{ VTABLE::BSLightingShaderMaterialLODLandscape };
		static constexpr auto FEATURE{ Feature::kLODLandscapeNoise };

		// members
		NiPointer<NiTexture> parentDiffuseTexture;   // C0
		NiPointer<NiTexture> parentNormalTexture;    // C8
		NiPointer<NiTexture> landscapeNoiseTexture;  // D0
		float                terrainTexOffsetX;       // D8
		float                terrainTexOffsetY;       // DC
		float                terrainTexFade;          // E0
		std::uint32_t        unkE4;                   // E4
		std::uint32_t        unkE8;                   // E8
		std::uint32_t        padEC;                   // EC
	};
	static_assert(sizeof(BSLightingShaderMaterialLODLandscape) == 0xF0);

	class __declspec(novtable) BSEffectShaderMaterial : public BSShaderMaterial
	{
	public:
		static constexpr auto RTTI{ RTTI::BSEffectShaderMaterial };
		static constexpr auto VTABLE{ VTABLE::BSEffectShaderMaterial };
		static constexpr auto TYPE{ Type::kEffect };

		// members
		float                falloffStartAngle;             // 38
		float                falloffStopAngle;              // 3C
		float                falloffStartOpacity;           // 40
		float                falloffStopOpacity;            // 44
		NiColorA             baseColor;                     // 48 (Alpha is Material Alpha)
		NiPointer<NiTexture> sourceTexture;                 // 58
		NiPointer<NiTexture> greyscaleTexture;              // 60
		NiPointer<NiTexture> envMapTexture;                 // 68
		NiPointer<NiTexture> envMaskTexture;                // 70
		NiPointer<NiTexture> normalTexture;                 // 78
		float                softDepth;                     // 80
		float                baseColorScale;                // 84
		BSFixedString        sourceTexturePath;             // 88
		BSFixedString        greyscaleTexturePath;          // 90
		BSFixedString        envMapTexturePath;             // 98
		BSFixedString        envMaskTexturePath;            // A0
		BSFixedString        normalTexturePath;             // A8
		union
		{
			float            envMapScale;                  // B0
			float            refractionPower;               // B0
		};
		std::uint8_t         textureClampMode;              // B4
		std::uint8_t         lightingInfluence;             // B5
		std::uint8_t         envMapMinLOD;                  // B6
		std::uint8_t         padB7;                         // B7
	};
	static_assert(sizeof(BSEffectShaderMaterial) == 0xB8);
}

#endif
