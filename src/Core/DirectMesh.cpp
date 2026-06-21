#include "Core/DirectMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

DirectMesh::DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
{
	if (bsTriShape->name.empty())
		m_Name = std::format("{}", fmt::ptr(bsTriShape)).c_str();
	else
		m_Name = bsTriShape->name.c_str();

	auto device = Renderer::GetSingleton()->GetDevice();

	auto blasDesc = nvrhi::rt::AccelStructDesc()
		.setIsTopLevel(false)
		.setDebugName(std::format("{} - BLAS", m_Name))
		.setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);

	// Geometry description
	{
		auto& geometryData = bsTriShape->GetGeometryRuntimeData();
		auto& trishapeData = bsTriShape->GetTrishapeRuntimeData();

		auto triShapeDX12 = reinterpret_cast<RE::BSGraphics::TriShapeDX12*>(geometryData.rendererData);

		const auto indexCount = trishapeData.triangleCount * 3;
		const auto indexStride = 2;
		const auto indexSize = indexStride * indexCount;

		const auto vertexCount = trishapeData.vertexCount;
		const auto vertexStride = Util::Geometry::GetStoredVertexSize(geometryData.vertexDesc);
		const auto vertexSize = vertexStride * vertexCount;

		auto& geometryTriangles = m_GeometryDesc.geometryData.triangles;

		// Index
		{
			auto indexBufferDesc = nvrhi::BufferDesc()
				.setByteSize(indexSize)
				.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
				.setIsAccelStructBuildInput(true)
				.setDebugName(std::format("{} (Triangle Buffer)", m_Name));

			m_IndexBuffer = device->createHandleForNativeBuffer(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(triShapeDX12->indexBufferDX12), indexBufferDesc);

			geometryTriangles.indexBuffer = m_IndexBuffer;
			geometryTriangles.indexOffset = 0;
			geometryTriangles.indexFormat = nvrhi::Format::R16_UINT;
			geometryTriangles.indexCount = indexCount;
		}

		// Vertex
		{
			auto vertexBufferDesc = nvrhi::BufferDesc()
				.setByteSize(vertexSize)
				.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
				.setIsAccelStructBuildInput(true)
				.setDebugName(std::format("{} (Vertex Buffer)", m_Name));

			m_VertexBuffer = device->createHandleForNativeBuffer(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(triShapeDX12->vertexBufferDX12), vertexBufferDesc);

			geometryTriangles.vertexBuffer = m_VertexBuffer;
			geometryTriangles.vertexOffset = 0;
			geometryTriangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
			geometryTriangles.vertexStride = vertexStride;
			geometryTriangles.vertexCount = vertexCount;
		}

		auto localToRoot = float3x4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f);

		m_GeometryDesc.setTransform(localToRoot.f);
	}

	blasDesc.addBottomLevelGeometry(m_GeometryDesc);

	m_BLAS = device->createAccelStruct(blasDesc);

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, m_BLAS, blasDesc);
}

void DirectMesh::SetHidden(bool hidden)
{
	m_State.set(hidden, State::Hidden);
}

bool DirectMesh::GetHidden() const
{
	return m_State.all(State::Hidden);
}

nvrhi::rt::IAccelStruct* DirectMesh::GetBLAS() const
{
	return m_BLAS;
}