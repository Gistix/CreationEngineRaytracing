#pragma once

struct InstancedData
{
	struct Instance
	{
		float3 position;
		float  scale;
		float  cosZ;
		float  sinZ;
		float  alpha;

		bool operator==(const Instance&) const = default;
	};

	eastl::vector<Instance> instances;
	bool changed = false;
};