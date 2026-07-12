#pragma once

#include "Core/DirectMesh.h"

class SubIndexMesh : public BaseMesh
{
	BufferDescriptor m_IndexBuffer;

	eastl::vector<nvrhi::rt::GeometryDesc> m_PrevGeometryDescs;

	ID3D12Resource* m_NativeIndexBuffer;

	ID3D12Resource* m_NativeVertexBuffer;

	uint16_t m_VertexStride;
protected:
	BufferDescriptor m_VertexBuffer;
public:
	SubIndexMesh(RE::BSSubIndexTriShape* subIndexTriShape, nvrhi::ICommandList* commandList);

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return static_cast<uint16_t>(m_IndexBuffer.m_Descriptor.Get()); }

	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get()); }

	SubIndexMesh* AsSubIndexMesh() override { return this; }

	void Update(nvrhi::ICommandList* commandList) override;
};
