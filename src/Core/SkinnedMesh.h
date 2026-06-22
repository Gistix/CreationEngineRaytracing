#pragma once

#include "Core/BaseMesh.h"

class SkinnedMesh : public BaseMesh
{
	nvrhi::BufferHandle m_VertexBuffer;
	eastl::vector<nvrhi::BufferHandle> m_IndexBuffers;
	eastl::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;
public:
	SkinnedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	void Update(nvrhi::ICommandList* commandList) override;
};
