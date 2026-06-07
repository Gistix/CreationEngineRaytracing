#pragma once

#include "Core/Instance.h"

// Since we have no release function we use a timer to track LOD lifetime
struct LODBlockReference
{
	inline static auto maxDetachedTime = std::chrono::seconds(15);

	eastl::vector<Instance*> instances;
	bool m_Attached;
	bool m_Hidden;
	std::chrono::time_point<std::chrono::steady_clock> detachedTime;

	virtual void UpdateVisibility() = 0;

	void SetAttached(bool attached)
	{
		for (auto& instance : instances) {
			instance->SetDetached(!attached);
		}

		if (!attached)
			detachedTime = std::chrono::steady_clock::now();

		m_Attached = attached;
	}
};