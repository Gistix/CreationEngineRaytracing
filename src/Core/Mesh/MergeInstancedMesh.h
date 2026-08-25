#pragma once

#include "Core/Mesh/BaseMesh.h"

#if defined(FALLOUT4)
#include "Types/RE/FO4/BSMergeInstancedTriShape.h"

class MergeInstancedMesh : public BaseMesh
{
	BufferDescriptor m_IndexBuffer;
	BufferDescriptor m_VertexBuffer;

	eastl::vector<uint32_t> m_ChunkMeshIndices;

public:
	MergeInstancedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);
	virtual ~MergeInstancedMesh() override;

	void OnDestroy() override;

	MergeInstancedMesh* AsMergeInstancedMesh() override { return this; }

	uint32_t GetShapeDataCount() const;
	const RE::BSMergeInstancedTriShape::ShapeData* GetShapeData() const;
	const uint16_t* GetLookupData() const;
	uint32_t GetLookupCount() const;

	uint16_t GetMeshIndex(size_t geometryIndex = 0) const override
	{
		return (geometryIndex < m_ChunkMeshIndices.size())
			? static_cast<uint16_t>(m_ChunkMeshIndices[geometryIndex])
			: m_MeshIndex;
	}

	uint16_t GetIndexID(size_t geometryIndex) const override;
	uint16_t GetVertexID() const override;

	void Update(nvrhi::ICommandList* commandList) override;
};
#endif
