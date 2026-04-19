#pragma once

namespace RE::BSGraphics 
{
	struct TextureData
	{
		uint16_t height;
		uint16_t width;
		uint8_t  unk1C;
		uint8_t  format;
		uint16_t unk1E;
	};

	class D3D12Texture : public RE::BSGraphics::Texture
	{
	public:
		ID3D12Resource* d3d12Texture;
	};
	static_assert(sizeof(D3D12Texture) == 0x30);
}