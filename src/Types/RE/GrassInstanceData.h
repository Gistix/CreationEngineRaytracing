#pragma once

#include "PCH.h"

namespace RE
{
	// CPU-side grass instance record as produced by GrassManager::CreateInstances and
	// handed to BSMultiStreamInstanceTriShape::AddGroup (4x half4 = 0x20 bytes).
	struct GrassInstanceData
	{
		half3    position;     // InstanceData1.xyz
		half     colorScale;   // InstanceData1.w
		half3    rot1;         // InstanceData2.xyz
		half3    rot2;         // InstanceData2.w, InstanceData3.xy
		half3    rot3;         // InstanceData3.zw, InstanceData4.x
		half     heightScale;  // InstanceData4.y
		uint32_t pad;          // InstanceData4.zw
	};
	static_assert(sizeof(GrassInstanceData) == 0x20);
}
