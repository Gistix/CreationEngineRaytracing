#pragma once

#include "Core/GrassInstance.h"

// Grass is awkward and it doesn't seem to be keep instance references anywhere but inside 'BSMultiStreamInstanceTriShape'
struct GrassReference
{
	eastl::vector<eastl::unordered_map<uint32_t, GrassInstance*>> m_InstanceGroups;
	bool m_Detached;
	bool m_Hidden;

	void BeginAddingInstances() { 
		m_InstanceGroups.push_back();
	};

	auto& GetCurrentGroup() 
	{ 
		return m_InstanceGroups[m_InstanceGroups.size() - 1];
	};
};