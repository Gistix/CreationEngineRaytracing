#include "Core/BaseMesh.h"
#include "Core/DirectMesh.h"
#include "Core/SkinnedMesh.h"
#include "Core/DynamicMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

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

nvrhi::BufferHandle BaseMesh::CreateIndexBuffer(RE::BSGraphics::TriShape* triShape)
{
	auto device = Renderer::GetSingleton()->GetDevice();

	auto triShapeDX12 = reinterpret_cast<RE::BSGraphics::TriShapeDX12*>(triShape);

	auto indexDesc = triShapeDX12->indexBufferDX12->GetDesc();

	D3D11_BUFFER_DESC indexDesc11;
	reinterpret_cast<ID3D11Buffer*>(triShapeDX12->indexBuffer)->GetDesc(&indexDesc11);

	if (indexDesc.Width != indexDesc11.ByteWidth) {
		logger::error("D3D11 ({}) and D3D12 ({}) index buffer size mismatch.", indexDesc11.ByteWidth, indexDesc.Width);
		return nullptr;
	}

	auto indexBufferDesc = nvrhi::BufferDesc()
		.setByteSize(indexDesc.Width)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true);

	return device->createHandleForNativeBuffer(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(triShapeDX12->indexBufferDX12), indexBufferDesc);
}

nvrhi::BufferHandle BaseMesh::CreateVertexBuffer(RE::BSGraphics::TriShape* triShape)
{
	auto device = Renderer::GetSingleton()->GetDevice();

	auto triShapeDX12 = reinterpret_cast<RE::BSGraphics::TriShapeDX12*>(triShape);

	auto vertexDesc = triShapeDX12->vertexBufferDX12->GetDesc();

	D3D11_BUFFER_DESC vertexDesc11;
	reinterpret_cast<ID3D11Buffer*>(triShapeDX12->vertexBuffer)->GetDesc(&vertexDesc11);

	if (vertexDesc.Width != vertexDesc11.ByteWidth) {
		logger::error("D3D11 ({}) and D3D12 ({}) vertex buffer size mismatch.", vertexDesc11.ByteWidth, vertexDesc.Width);
		return nullptr;
	}

	auto vertexBufferDesc = nvrhi::BufferDesc()
		.setByteSize(vertexDesc.Width)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true);

	return device->createHandleForNativeBuffer(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(triShapeDX12->vertexBufferDX12), vertexBufferDesc);
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
	for (auto& desc : m_GeometryDescs)
		desc.setTransform(m_LocalToOwner.f);

	if (changed)
		MarkDirty(DirtyFlags::Transform);
}