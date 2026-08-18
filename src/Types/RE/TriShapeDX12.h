#pragma once

#include "PCH.h"

namespace RE::BSGraphics
{
#if defined(SKYRIM)
	// An extension of RE::BSGraphics::TriShape for D3D12
	struct TriShapeDX12 : TriShape
	{
		ID3D12Resource* vertexBufferDX12;
		ID3D12Resource* indexBufferDX12;
		bool ownsDX12Buffers = false;
	};
	static_assert(sizeof(TriShapeDX12) == 0x48);
#elif defined(FALLOUT4)
	// An extension of RE::BSGraphics::Buffer for D3D12
	struct BufferDX12 : Buffer
	{
		ID3D12Resource* bufferDX12{ nullptr };
	};
	static_assert(sizeof(BufferDX12) == 0x58);

	struct VertexBufferDX12 : BufferDX12 {};
	struct IndexBufferDX12 : BufferDX12 {};
#endif
}
