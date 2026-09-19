#pragma once

#include "Core/BLASCluster.h"
#include "Core/Mesh/InstancedMesh.h"

class BLASInstanceCluster : public BLASCluster
{
public:
	explicit BLASInstanceCluster(RE::TESObjectREFR* owner);

	uint32_t GetInstanceCount() const override;

	uint32_t Update() override;

	void AppendInstanceDescs(eastl::vector<nvrhi::rt::InstanceDesc>& outDescs) const override;

	void WriteInstanceData(uint32_t firstMesh, uint32_t meshCount, InstanceData* outInstances, float4* outBounds) const override;
};
