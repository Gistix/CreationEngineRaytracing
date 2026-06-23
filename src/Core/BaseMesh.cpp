#include "Core/BaseMesh.h"
#include "Core/DirectMesh.h"
#include "Core/SkinnedMesh.h"
#include "Core/DynamicMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Types/RE/RE.h"
#include "interop/Triangle.hlsli"

eastl::shared_ptr<BaseMesh> BaseMesh::Create(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
{
	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();

	if (geometryData.rendererData)
		return eastl::make_shared<DirectMesh>(bsTriShape, commandList);

	if (auto bsDynamicTriShape = bsTriShape->AsDynamicTriShape())
		return eastl::make_shared<DynamicMesh>(bsDynamicTriShape, commandList);

	if (geometryData.skinInstance.get())
		return eastl::make_shared<SkinnedMesh>(bsTriShape, commandList);

	logger::warn("BaseMesh::Create - No renderer data or skin instance for {}", MakeDebugName(bsTriShape));
	return nullptr;
}

eastl::string BaseMesh::MakeDebugName(RE::BSTriShape* bsTriShape)
{
	if (bsTriShape->name.empty())
		return { std::format("{}", fmt::ptr(bsTriShape)).c_str() };

	return { bsTriShape->name.c_str() };
}

uint32_t BaseMesh::WriteMeshData(MeshData* out) const
{
	const auto& descs = GetGeometryDescs();

	const uint16_t vertexID = GetVertexID();

	for (size_t i = 0; i < descs.size(); i++) {
		MeshData& md = out[i];
		md = {};

		md.IndexID = GetIndexID(i);
		md.VertexID = vertexID;

		std::memcpy(&md.VertexDesc, &m_VertexDescRaw, sizeof(md.VertexDesc));

		md.NumTriangles = descs[i].geometryData.triangles.indexCount / 3;
		md.Transform = m_LocalToOwner;
		md.PrevTransform = m_LocalToOwner;
	}

	return static_cast<uint32_t>(descs.size());
}

bool BaseMesh::ValidateCounts(uint16_t numTriangles, uint32_t numVertices, RE::BSGraphics::TriShape* triShape)
{
	if (numTriangles == 0) {
		logger::warn("BaseMesh::ValidateCounts - Num triangles equals 0, skipping.");
		return false;
	}

	if (numVertices == 0) {
		logger::warn("BaseMesh::ValidateCounts - Num vertices equals 0, skipping.");
		return false;
	}

	if (triShape->pad1C != 1) {
		logger::warn("BaseMesh::ValidateCounts - Missing sentinel value, skipping.");
		return false;
	}

	return true;
}

BaseMesh::BufferDescriptor BaseMesh::CreateIndexBuffer(RE::BSGraphics::TriShape* triShape)
{
	BufferDescriptor indexBuffer{};

	auto triShapeDX12 = reinterpret_cast<RE::BSGraphics::TriShapeDX12*>(triShape);

	auto indexDesc = triShapeDX12->indexBufferDX12->GetDesc();

	D3D11_BUFFER_DESC indexDesc11;
	reinterpret_cast<ID3D11Buffer*>(triShapeDX12->indexBuffer)->GetDesc(&indexDesc11);

	if (indexDesc.Width != indexDesc11.ByteWidth) {
		logger::error("D3D11 ({}) and D3D12 ({}) index buffer size mismatch.", indexDesc11.ByteWidth, indexDesc.Width);
		return indexBuffer;
	}

	auto indexBufferDesc = nvrhi::BufferDesc()
		.setByteSize(indexDesc.Width)
		.setStructStride(sizeof(Triangle))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true);

	auto device = Renderer::GetSingleton()->GetDevice();
	indexBuffer.m_Buffer = device->createHandleForNativeBuffer(
		nvrhi::ObjectTypes::D3D12_Resource, 
		nvrhi::Object(triShapeDX12->indexBufferDX12), 
		indexBufferDesc);

	if (indexBuffer.m_Buffer) {
		auto& descriptorTable = Scene::GetSingleton()->GetSceneGraph()->GetTriangleDescriptors()->m_DescriptorTable;
		indexBuffer.m_Descriptor = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, indexBuffer.m_Buffer));
	}

	return indexBuffer;
}

BaseMesh::BufferDescriptor BaseMesh::CreateVertexBuffer(RE::BSGraphics::TriShape* triShape)
{
	BufferDescriptor vertexBuffer{};

	auto triShapeDX12 = reinterpret_cast<RE::BSGraphics::TriShapeDX12*>(triShape);

	auto vertexDesc = triShapeDX12->vertexBufferDX12->GetDesc();

	D3D11_BUFFER_DESC vertexDesc11;
	reinterpret_cast<ID3D11Buffer*>(triShapeDX12->vertexBuffer)->GetDesc(&vertexDesc11);

	if (vertexDesc.Width != vertexDesc11.ByteWidth) {
		logger::error("D3D11 ({}) and D3D12 ({}) vertex buffer size mismatch.", vertexDesc11.ByteWidth, vertexDesc.Width);
		return vertexBuffer;
	}

	auto vertexBufferDesc = nvrhi::BufferDesc()
		.setByteSize(vertexDesc.Width)
		.setCanHaveRawViews(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true);

	auto device = Renderer::GetSingleton()->GetDevice();
	vertexBuffer.m_Buffer = device->createHandleForNativeBuffer(
		nvrhi::ObjectTypes::D3D12_Resource, 
		nvrhi::Object(triShapeDX12->vertexBufferDX12), 
		vertexBufferDesc);

	if (vertexBuffer.m_Buffer) {
		auto& descriptorTable = Scene::GetSingleton()->GetSceneGraph()->GetVertexDescriptors()->m_DescriptorTable;
		vertexBuffer.m_Descriptor = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::RawBuffer_SRV(0, vertexBuffer.m_Buffer));
	}

	return vertexBuffer;
}

nvrhi::rt::GeometryDesc BaseMesh::MakeGeometryDesc(nvrhi::IBuffer* indexBuffer, uint32_t indexCount, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, uint32_t vertexCount)
{
	nvrhi::rt::GeometryDesc geometryDesc;

	auto& geometryTriangles = geometryDesc.geometryData.triangles;

	geometryTriangles.indexBuffer = indexBuffer;
	geometryTriangles.indexOffset = 0;
	geometryTriangles.indexFormat = nvrhi::Format::R16_UINT;
	geometryTriangles.indexCount = indexCount;

	geometryTriangles.vertexBuffer = vertexBuffer;
	geometryTriangles.vertexOffset = 0;
	geometryTriangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
	geometryTriangles.vertexStride = vertexStride;
	geometryTriangles.vertexCount = vertexCount;

	auto localToRoot = float3x4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f);

	geometryDesc.setTransform(localToRoot.f);

	return geometryDesc;
}

bool BaseMesh::SetHidden(bool hidden)
{
	const bool wasHidden = m_State.any(State::Hidden);

	m_State.set(hidden, State::Hidden);

	if (wasHidden != hidden) {
		// A visibility change alters which geometry the cluster includes -> rebuild.
		MarkDirty(DirtyFlags::Visibility);
		return true;
	}

	return false;
}

bool BaseMesh::IsHidden() const
{
	return m_State.any(State::Hidden);
}

void BaseMesh::OnDestroy() {
	std::scoped_lock lock(m_BSTriShapeMutex);
	m_BSTriShape = nullptr;
}

bool BaseMesh::SetOwner(RE::TESObjectREFR* owner)
{
	if (m_Owner == owner)
		return false;

	m_PrevOwner = m_Owner;
	m_Owner = owner;

	// Owner change re-buckets the mesh into another cluster -> both clusters rebuild.
	MarkDirty(DirtyFlags::Visibility);

	return true;
}

void BaseMesh::SetLocalToOwner(const float3x4& localToOwner)
{
	const bool changed = !Util::Math::MatrixNearEqual(m_LocalToOwner, localToOwner);

	m_LocalToOwner = localToOwner;

	// Always (re)bake into the geometry descs so the layout is correct even on the first set;
	// only flag a refit when it actually changed.
	for (auto& desc : GetGeometryDescsMutable())
		desc.setTransform(m_LocalToOwner.f);

	if (changed)
		MarkDirty(DirtyFlags::Transform);
}