#pragma once

#include "Core/Instance.h"

// Since we have no release function we use a timer to track LOD lifetime
struct LODBlockReference
{
	inline static auto maxDetachedTime = std::chrono::seconds(15);

	eastl::vector<Instance*> instances;
	bool detached;
	bool m_Hidden;
	std::chrono::time_point<std::chrono::steady_clock> detachedTime;

	void UpdateVisibility(RE::BSMultiBoundNode* node);
};