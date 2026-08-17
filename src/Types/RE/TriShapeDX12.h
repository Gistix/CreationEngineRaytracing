#pragma once

#include "PCH.h"

namespace RE::BSGraphics
{
	// An extesion of RE::BSGraphics::TriShape for D3D12
	struct TriShapeDX12 : TriShape
	{
		ID3D12Resource* vertexBufferDX12;
		ID3D12Resource* indexBufferDX12;
		bool ownsDX12Buffers = false;
	};
#if defined(SKYRIM)
	static_assert(sizeof(TriShapeDX12) == 0x48);
#elif defined(FALLOUT4)
	static_assert(sizeof(TriShapeDX12) == 0x38);
#endif
}
