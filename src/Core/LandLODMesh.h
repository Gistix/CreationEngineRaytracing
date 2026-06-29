#pragma once

#include "Core/DirectMesh.h"
#include "Interop/LandLODUpdate.hlsli"

class LandLODMesh : public DirectMesh
{
	nvrhi::BufferHandle m_LiveVertexBuffer;
	bool m_Intersecting = false;
	bool m_PrevIntersecting = false;
	float2 m_AABBCenter;
	float2 m_AABBSize;
	bool m_NeedsOcclusionPass = false;
public:
	LandLODMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	bool IsUpdatable() const override { return true; }

	bool Update(BLASCluster* cluster) override;
	void UpdateOcclusion(const float3x4& clusterTransform);
};
