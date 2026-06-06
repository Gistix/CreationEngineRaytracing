#pragma once

#include "Core/Instance.h"

// Since we have no release function we use a timer to track LOD lifetime
struct LODBlockReference
{
	inline static auto maxDetachedTime = std::chrono::seconds(15);

	eastl::vector<Instance*> instances;
	bool m_Detached;
	bool m_Hidden;
	std::chrono::time_point<std::chrono::steady_clock> detachedTime;

	virtual void UpdateVisibility() { };

	void SetDetached(bool detached)
	{
		if (detached && detached != m_Detached)
			detachedTime = std::chrono::steady_clock::now();

		m_Detached = detached;
	}
};