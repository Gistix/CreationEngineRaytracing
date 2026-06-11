#pragma once

#if defined(FALLOUT4)

namespace RE
{
	namespace BSGraphics
	{
		class Texture
		{
		public:
			void*                      vtable;        // 00
			ID3D11Texture2D*           texture;       // 08
			ID3D11UnorderedAccessView* UAV;           // 10
			ID3D11ShaderResourceView*  resourceView;  // 18
			std::uint16_t              width;         // 20
			std::uint16_t              height;        // 22
			std::uint8_t               format;        // 24
			std::uint8_t               mips;          // 25
			std::uint16_t              unk1E;         // 26
			std::uint32_t              refCount;      // 28
			std::uint32_t              pad24;         // 2C
		};
		static_assert(sizeof(Texture) == 0x30);
	}
}

#endif
