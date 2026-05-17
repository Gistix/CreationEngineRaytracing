#pragma once

#include "PCH.h"

#include "Core/Reference/LODBlockReference.h"

struct GrassRefrKey
{
	RE::FormID id;
	std::int16_t x;
	std::int16_t y;
};

struct GrassReference
{
	struct InstanceData
	{
		half3 position; 
		half colorScale;

		half3 rot1;
		half3 rot2;
		half3 rot3;

		half heightScale;

		std::uint16_t unk1C;
		std::uint16_t unk1E;
	};
	static_assert(sizeof(InstanceData) == 0x20);

	eastl::vector<Instance*> m_Instances;
	eastl::vector<InstanceData> m_InstanceData;

	bool m_Detached;
	bool m_Hidden;

	virtual void UpdateVisibility();
};