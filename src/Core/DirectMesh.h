#pragma once

#include "Core/BaseMesh.h"

class DirectMesh : public BaseMesh
{
	nvrhi::BufferHandle m_IndexBuffer;
	nvrhi::BufferHandle m_VertexBuffer;
	nvrhi::rt::GeometryDesc m_GeometryDesc;
public:
	DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);
};
