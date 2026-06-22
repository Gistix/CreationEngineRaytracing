#include "Core/DirectMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

DirectMesh::DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
{
	if (bsTriShape->name.empty())
		m_Name = { std::format("{}", fmt::ptr(bsTriShape)).c_str() };
	else
		m_Name = { bsTriShape->name.c_str() };

	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();
	if (auto rendererData = geometryData.rendererData) {
		CreateGeometryDescs(bsTriShape, rendererData);
	}
	else if (auto skinInstance = geometryData.skinInstance.get()) {
		CreateSkinnedGeometryDescs(skinInstance);
	}

	if (m_RendererData.empty()) {
		logger::warn("DirectMesh::DirectMesh - No renderer data for {}", m_Name);
		return;
	}

	auto blasDesc = nvrhi::rt::AccelStructDesc()
		.setIsTopLevel(false)
		.setDebugName(std::format("{} - BLAS", m_Name.c_str()))
		.setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);

	for (const auto& rendererData : m_RendererData)
	{
		blasDesc.addBottomLevelGeometry(rendererData.m_GeometryDesc);
	}

	m_BLAS = Renderer::GetSingleton()->GetDevice()->createAccelStruct(blasDesc);

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, m_BLAS, blasDesc);
}

void DirectMesh::CreateGeometryDescs(RE::BSTriShape* bsTriShape, RE::BSGraphics::TriShape* triShape)
{
	const auto& triShapeData = bsTriShape->GetTrishapeRuntimeData();

	RendererData rendererData;
	if (CreateRendererData(triShapeData.triangleCount, triShapeData.vertexCount, triShape, rendererData))
		m_RendererData.push_back(rendererData);
}

void DirectMesh::CreateSkinnedGeometryDescs(RE::NiSkinInstance* skinInstance)
{
	const auto& skinPartition = skinInstance->skinPartition;

	for (size_t i = 0; i < skinPartition->numPartitions; i++)
	{
		const auto& partition = skinPartition->partitions[i];

		RendererData rendererData;
		if (CreateRendererData(partition.triangles, skinPartition->vertexCount, partition.buffData, rendererData))
			m_RendererData.push_back(rendererData);
	}
}

bool DirectMesh::CreateRendererData(uint16_t numTriangles, uint32_t numVertices, RE::BSGraphics::TriShape* triShape, RendererData& rendererData)
{
	if (numTriangles == 0) {
		logger::warn("DirectMesh::CreateRendererData - Num triangles equals 0, skipping.");
		return false;
	}

	if (numVertices == 0) {
		logger::warn("DirectMesh::CreateRendererData - Num vertices equals 0, skipping.");
		return false;
	}

	if (triShape->pad1C != 1) {
		logger::warn("DirectMesh::CreateRendererData - Missing sentinel value, skipping.");
		return false;
	}

	auto device = Renderer::GetSingleton()->GetDevice();

	// Promote to uint32_t to avoid overflow
	const uint32_t indexCount = numTriangles * 3;
	//const uint16_t indexStride = 2;
	//const uint16_t indexSize = indexStride * indexCount;

	// NiSkinPartition::vertexCount requires uint32_t 
	const uint32_t vertexCount = numVertices;
	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(triShape->vertexDesc);
	//const uint16_t vertexSize = vertexStride * vertexCount;

	auto triShapeDX12 = reinterpret_cast<RE::BSGraphics::TriShapeDX12*>(triShape);

	auto indexDesc = triShapeDX12->indexBufferDX12->GetDesc();
	auto vertexDesc = triShapeDX12->vertexBufferDX12->GetDesc();

	D3D11_BUFFER_DESC indexDesc11;
	reinterpret_cast<ID3D11Buffer*>(triShapeDX12->indexBuffer)->GetDesc(&indexDesc11);

	D3D11_BUFFER_DESC vertexDesc11;
	reinterpret_cast<ID3D11Buffer*>(triShapeDX12->vertexBuffer)->GetDesc(&vertexDesc11);

	if (indexDesc.Width != indexDesc11.ByteWidth) {
		logger::error("D3D11 ({}) and D3D12 ({}) index buffer size mismatch.", indexDesc11.ByteWidth, indexDesc.Width);
		return false;
	}

	if (vertexDesc.Width != vertexDesc11.ByteWidth) {
		logger::error("D3D11 ({}) and D3D12 ({}) vertex buffer size mismatch.", vertexDesc11.ByteWidth, vertexDesc.Width);
		return false;
	}

	auto& geometryTriangles = rendererData.m_GeometryDesc.geometryData.triangles;

	// Index
	{
		auto indexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(indexDesc.Width)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
			.setIsAccelStructBuildInput(true);

		rendererData.m_IndexBuffer = device->createHandleForNativeBuffer(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(triShapeDX12->indexBufferDX12), indexBufferDesc);

		geometryTriangles.indexBuffer = rendererData.m_IndexBuffer;
		geometryTriangles.indexOffset = 0;
		geometryTriangles.indexFormat = nvrhi::Format::R16_UINT;
		geometryTriangles.indexCount = indexCount;
	}

	// Vertex
	{
		auto vertexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(vertexDesc.Width)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
			.setIsAccelStructBuildInput(true);

		rendererData.m_VertexBuffer = device->createHandleForNativeBuffer(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(triShapeDX12->vertexBufferDX12), vertexBufferDesc);

		geometryTriangles.vertexBuffer = rendererData.m_VertexBuffer;
		geometryTriangles.vertexOffset = 0;
		geometryTriangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
		geometryTriangles.vertexStride = vertexStride;
		geometryTriangles.vertexCount = vertexCount;
	}

	auto localToRoot = float3x4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f);

	rendererData.m_GeometryDesc.setTransform(localToRoot.f);

	return true;
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