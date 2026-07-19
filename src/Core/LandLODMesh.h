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

	// Last submitted occlusion update inputs; the occluder is only re-run (and the
	// BLAS refit only triggered) when these change. The occluder is stateless given
	// (original vertices, transform, loadedRange), so skipping identical updates
	// leaves the live vertex buffer valid.
	bool m_OcclusionUpdateWritten = false;
	float3x4 m_LastOcclusionTransform = Constants::kIdentityTransform;
	float4 m_LastLoadedRange = float4(0.0f, 0.0f, 0.0f, 0.0f);
public:
	LandLODMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	bool IsUpdatable() const override { return true; }

	void Update(nvrhi::ICommandList* commandList) override;
	void UpdateOcclusion(const float4& loadedRange);
};
