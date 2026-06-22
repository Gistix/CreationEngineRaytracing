#pragma once

#include "Core/BaseMesh.h"

class DirectMesh : public BaseMesh
{
	nvrhi::BufferHandle m_IndexBuffer;
	nvrhi::BufferHandle m_VertexBuffer;
public:
	DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);
};
