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
	}
}

#endif
