#pragma once

#include "Light.hlsli"
#include "Core/Instance.h"

struct Light
{
	RE::BSLight* m_Light;

	// Whether the light is active or not, only active lights build a instance list
	bool m_Active;

	// Light data index
	uint8_t m_Index;

	// Instances that are affected by the light, calculated from instance node worldBound center + radius and light position + radius
	eastl::hash_set<Instance*> m_Instances;

	eastl::hash_set<Instance*> m_PrevInstances;
	nvrhi::rt::AccelStructHandle m_TopLevelAS;
	uint64_t m_LastUpdate = 0;

	eastl::vector<nvrhi::rt::InstanceDesc> m_InstanceDescs;
	uint32_t m_TopLevelInstances = 0;


	bool m_DirtyBinding;

	void UpdateTLAS(nvrhi::ICommandList* commandList);
	void UpdateInstances();
};