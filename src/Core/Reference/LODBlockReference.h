#pragma once

#include "Core/Instance.h"

// Since we have no release function we use a timer to track LOD lifetime
struct LODBlockReference
{
	inline static auto maxDetachedTime = std::chrono::seconds(15);

	LODBlockReference(bool attached)
		: m_Attached(attached) { }

	~LODBlockReference();

	auto& GetInstances() const
	{
		return instances;
	}

	void AddInstance(Instance* instance);

	virtual void UpdateVisibility() = 0;

protected:
	eastl::vector<Instance*> instances;
	bool m_Attached;
	bool m_Hidden;

	void SetAttached(bool attached);
};