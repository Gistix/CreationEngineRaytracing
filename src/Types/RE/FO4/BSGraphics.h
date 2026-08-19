#pragma once

#if defined(FALLOUT4)

#include "RE/B/BSGraphics.h"

namespace RE
{
	namespace BSGraphics
	{
		struct TextureStreamData
		{
			std::uint32_t refCount;       // 00
			std::uint8_t  dataFileIndex;  // 04
			std::uint8_t  chunkCount;     // 05
			std::uint16_t minLOD;         // 06
		};
		static_assert(sizeof(TextureStreamData) == 0x8);

		class Texture
		{
		public:
			ID3D11ShaderResourceView*	   resourceView;       // 00
			ID3D11Texture2D*			   texture;            // 08
			ID3D11UnorderedAccessView*	   UAV;                // 10
			TextureStreamData*			   streamData;         // 18
			BSEventFlag*				   requestEventToWait; // 20
			TextureHeader		           header;	           // 28
			std::uint32_t				   pendingRequests;    // 30
			std::uint32_t				   refCount;           // 34
			std::uint32_t				   creationFrame;      // 38
			std::uint8_t				   minLOD;             // 3C
			std::uint8_t				   degradeLevel;       // 3D
			std::uint8_t				   desiredDegradeLevel;// 3E
			std::uint8_t				   flags;              // 3F
		};
		static_assert(sizeof(Texture) == 0x40);

		struct RenderTargets {
			enum RenderTarget
			{
				kFrameBuffer = 0,

				kRefractionNormal = 1,

				kMainPreAlpha = 2,
				kMain = 3,
				kMainTemp = 4,

				kSSRRaw = 7,
				kSSRBlurred = 8,
				kSSRBlurredExtra = 9,

				kMainVerticalBlur = 14,
				kMainHorizontalBlur = 15,

				kSSRDirection = 10,
				kSSRMask = 11,

				kUI = 17,
				kUITemp = 18,

				kGbufferNormal = 20,
				kGbufferNormalSwap = 21,
				kGbufferAlbedo = 22,
				kGbufferEmissive = 23,
				kGbufferMaterial = 24, //  Glossiness, Specular, Backlighting, SSS

				kSSAO = 28,

				kTAAAccumulation = 26,
				kTAAAccumulationSwap = 27,

				kMotionVectors = 29,

				kUIDownscaled = 36,
				kUIDownscaledComposite = 37,

				kMainDepthMips = 39,

				kUnkMask = 57,

				kSSAOTemp = 48,
				kSSAOTemp2 = 49,
				kSSAOTemp3 = 50,

				kDiffuseBuffer = 58,
				kSpecularBuffer = 59,

				kDownscaledHDR = 64,
				kDownscaledHDRLuminance2 = 65,
				kDownscaledHDRLuminance3 = 66,
				kDownscaledHDRLuminance4 = 67,
				kDownscaledHDRLuminance5Adaptation = 68,
				kDownscaledHDRLuminance6AdaptationSwap = 69,
				kDownscaledHDRLuminance6 = 70,

				kCount = 101
			};
		};

		struct DepthStencilTargets {
			enum DepthStencilTarget
			{
				kMainOtherOther = 0,
				kMainOther = 1,
				kMain = 2,
				kMainCopy = 3,
				kMainCopyCopy = 4,

				kShadowMap = 8,

				kCount = 13
			};
		};
	}
}

#endif
