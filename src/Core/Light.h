#pragma once

#include "Light.hlsli"
#include "Core/Instance.h"

struct Light
{
	RE::BSLight* m_Light;
	eastl::hash_set<Instance*> m_Instances;

	eastl::hash_set<Instance*> m_PrevInstances;
	nvrhi::rt::AccelStructHandle m_TopLevelAS;
	uint64_t m_LastUpdate = 0;

	eastl::vector<nvrhi::rt::InstanceDesc> m_InstanceDescs;
	uint32_t m_TopLevelInstances = 0;

	uint8_t m_Index;
	bool m_DirtyBinding;

	bool m_Active;

	void UpdateTLAS(nvrhi::ICommandList* commandList);
	void UpdateInstances();
};