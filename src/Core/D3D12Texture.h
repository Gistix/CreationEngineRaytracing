#pragma once

namespace RE::BSGraphics 
{
	class D3D12Texture : public RE::BSGraphics::Texture
	{
	public:
		ID3D12Resource* d3d12Texture;
	};
	static_assert(sizeof(D3D12Texture) == 0x30);
}