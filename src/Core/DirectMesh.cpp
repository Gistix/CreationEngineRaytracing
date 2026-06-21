#include "Core/DirectMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

DirectMesh::DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
{
	if (bsTriShape->name.empty())
		m_Name = std::format("{}", fmt::ptr(bsTriShape)).c_str();
	else
		m_Name = { bsTriShape->name.c_str() };

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

		// Promote to uint32_t to avoid overflow
		const uint32_t indexCount = trishapeData.triangleCount * 3;
		//const uint16_t indexStride = 2;
		//const uint16_t indexSize = indexStride * indexCount;

		const uint16_t vertexCount = trishapeData.vertexCount;
		const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(geometryData.vertexDesc);
		//const uint16_t vertexSize = vertexStride * vertexCount;

		auto indexDesc = triShapeDX12->indexBufferDX12->GetDesc();
		auto vertexDesc = triShapeDX12->vertexBufferDX12->GetDesc();

		D3D11_BUFFER_DESC indexDesc11;
		reinterpret_cast<ID3D11Buffer*>(triShapeDX12->indexBuffer)->GetDesc(&indexDesc11);

		D3D11_BUFFER_DESC vertexDesc11;
		reinterpret_cast<ID3D11Buffer*>(triShapeDX12->vertexBuffer)->GetDesc(&vertexDesc11);

		if (indexDesc.Width != indexDesc11.ByteWidth) {
			logger::error("D3D11 ({}) and D3D12 ({}) index buffer size mismatch.", indexDesc11.ByteWidth, indexDesc.Width);
			return;
		}

		if (vertexDesc.Width != vertexDesc11.ByteWidth) {
			logger::error("D3D11 ({}) and D3D12 ({}) vertex buffer size mismatch.", vertexDesc11.ByteWidth, vertexDesc.Width);
			return;
		}

		auto& geometryTriangles = m_GeometryDesc.geometryData.triangles;

		// Index
		{
			auto indexBufferDesc = nvrhi::BufferDesc()
				.setByteSize(indexDesc.Width)
				.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
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
				.setByteSize(vertexDesc.Width)
				.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
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

bool DirectMesh::IsHidden() const
{
	return m_State.any(State::Hidden);
}

nvrhi::rt::IAccelStruct* DirectMesh::GetBLAS() const
{
	return m_BLAS;
}