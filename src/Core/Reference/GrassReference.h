#pragma once

#include "PCH.h"

#include "core/Instance.h"

struct GrassReference
{
	struct InstanceData
	{
		half3   position;  // InstanceData1.xyz
		half  colorScale;  // InstanceData1.w

		half3       rot1;  // InstanceData2.xyz
		half3       rot2;  // InstanceData2.w, InstanceData3.xy
		half3       rot3;  // InstanceData3.zw, InstanceData4.x

		half heightScale;  // InstanceData4.y

		uint32_t     pad;  // InstanceData4.zw
	};
	static_assert(sizeof(InstanceData) == 0x20);

	eastl::vector<Instance*> m_Instances;
	eastl::vector<InstanceData> m_InstanceData;

	bool m_Detached;
	bool m_Hidden;

	virtual void UpdateVisibility();
};