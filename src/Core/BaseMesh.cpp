#include "Core/BaseMesh.h"
#include "Core/DirectMesh.h"
#include "Core/LandLODMesh.h"
#include "Core/SkinnedMesh.h"
#include "Core/DynamicMesh.h"
#include "Core/SubIndexMesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Types/RE/RE.h"
#include "interop/Triangle.hlsli"

eastl::unique_ptr<BaseMesh> BaseMesh::Create(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
{
	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();

	if (geometryData.rendererData) {
		if (auto* extra = bsTriShape->GetExtraData<RE::NiIntegersExtraData>(Constants::ExtraData::LandLOD)) {
			if (extra->size > 0 && extra->value[0] == 4)
				return eastl::make_unique<LandLODMesh>(bsTriShape, commandList);
		}

		if (auto* subIndexTriShape = Util::Adapter::AsSubIndexTriShape(bsTriShape))
			return eastl::make_unique<SubIndexMesh>(subIndexTriShape, Scene::GetSingleton()->GetSceneGraph());

		return eastl::make_unique<DirectMesh>(bsTriShape, commandList);
	}

	if (auto bsDynamicTriShape = bsTriShape->AsDynamicTriShape())
		return eastl::make_unique<DynamicMesh>(bsDynamicTriShape, commandList);

	if (geometryData.skinInstance.get())
		return eastl::make_unique<SkinnedMesh>(bsTriShape, commandList);

	logger::warn("BaseMesh::Create - No renderer data or skin instance for {}", MakeDebugName(bsTriShape));
	return nullptr;
}

eastl::string BaseMesh::MakeDebugName(RE::BSTriShape* bsTriShape)
{
	if (bsTriShape->name.empty())
		return { std::format("{}", fmt::ptr(bsTriShape)).c_str() };

	return { bsTriShape->name.c_str() };
}

void BaseMesh::UpdateLocalTransform(const float4x4& invTransform, const float4x4& prevInvTransform)
{
	XMStoreFloat3x4(&m_LocalTransform,
		XMMatrixMultiply(XMLoadFloat3x4(&m_Transform), invTransform));

	XMStoreFloat3x4(&m_PrevLocalTransform,
		XMMatrixMultiply(XMLoadFloat3x4(&m_PrevTransform), prevInvTransform));

	for (auto& desc: m_GeometryDescs)
	{
		desc.setTransform(m_LocalTransform.f);
	}
}

uint32_t BaseMesh::WriteMeshData(MeshData* out) const
{
	using namespace DirectX;

	const auto& descs = GetGeometryDescs();

	const uint16_t vertexID = GetVertexID();

	for (size_t i = 0; i < descs.size(); i++) {
		auto& geomTris = descs[i].geometryData.triangles;

		out[i] = {
			GetIndexID(i),
			vertexID,
			VertexDesc(GetVertexDescRaw()),
			static_cast<uint16_t>(geomTris.vertexCount),
			static_cast<uint16_t>(geomTris.indexCount / 3),
			m_Properties.GetData(),
			static_cast<uint16_t>(m_Type),
			static_cast<uint16_t>(GetDynamicIndex()),
			m_Material->GetOffsetComp(),
			m_LocalTransform,
			m_PrevLocalTransform
		};
	}

	return static_cast<uint32_t>(descs.size());
}

void BaseMesh::MarkDirty(DirtyFlags flag) {
	m_DirtyFlags.set(flag);
	Scene::GetSingleton()->GetSceneGraph()->MarkClusterDirty(m_Cluster);
}

bool BaseMesh::ValidateCounts(uint16_t numTriangles, uint32_t numVertices)
{
	if (numTriangles == 0) {
		logger::warn("BaseMesh::ValidateCounts - Num triangles equals 0, skipping.");
		return false;
	}

	if (numVertices == 0) {
		logger::warn("BaseMesh::ValidateCounts - Num vertices equals 0, skipping.");
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
		.setIsAccelStructBuildInput(true)
		.setDebugName("Index Buffer");

	auto device = Renderer::GetSingleton()->GetDevice();
	indexBuffer.m_Buffer = device->createHandleForNativeBuffer(
		nvrhi::ObjectTypes::D3D12_Resource, 
		nvrhi::Object(triShapeDX12->indexBufferDX12), 
		indexBufferDesc);

	if (indexBuffer.m_Buffer) {
		auto& descriptorTable = Scene::GetSingleton()->GetSceneGraph()->GetTriangleDescriptors()->m_DescriptorTable;
		indexBuffer.m_Descriptor = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, indexBuffer.m_Buffer));
	}
	else {
		logger::error("BaseMesh::CreateIndexBuffer - Failed to create handle for native buffe;");
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
		.setIsAccelStructBuildInput(true)
		.setDebugName("Vertex Buffer");

	auto device = Renderer::GetSingleton()->GetDevice();
	vertexBuffer.m_Buffer = device->createHandleForNativeBuffer(
		nvrhi::ObjectTypes::D3D12_Resource, 
		nvrhi::Object(triShapeDX12->vertexBufferDX12), 
		vertexBufferDesc);

	if (vertexBuffer.m_Buffer) {
		auto& descriptorTable = Scene::GetSingleton()->GetSceneGraph()->GetVertexDescriptors()->m_DescriptorTable;
		vertexBuffer.m_Descriptor = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::RawBuffer_SRV(0, vertexBuffer.m_Buffer));
	}
	else {
		logger::error("BaseMesh::CreateIndexBuffer - Failed to create handle for native buffe;");
	}

	return vertexBuffer;
}

void BaseMesh::Update([[ maybe_unused ]] nvrhi::ICommandList* commandList)
{ 
	ClearDirtyFlags();

	m_Properties = { m_BSTriShape };

	m_WorldBound = m_BSTriShape->worldBound;

	float3x4 transform;
	XMStoreFloat3x4(&transform, Util::Math::GetXMFromNiTransform(m_BSTriShape->world));

	if (!Util::Math::MatrixNearEqual(transform, m_Transform))
		MarkDirty(DirtyFlags::Transform);

	m_Transform = transform;
	XMStoreFloat3x4(&m_PrevTransform, Util::Math::GetXMFromNiTransform(m_BSTriShape->previousWorld));

	UpdateMaterial();
}

nvrhi::rt::GeometryDesc BaseMesh::MakeGeometryDesc(nvrhi::IBuffer* indexBuffer, uint32_t indexOffset, uint32_t indexCount, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, uint32_t vertexCount)
{
	nvrhi::rt::GeometryDesc geometryDesc;

	auto& geometryTriangles = geometryDesc.geometryData.triangles;

	geometryTriangles.indexBuffer = indexBuffer;
	geometryTriangles.indexOffset = indexOffset * sizeof(uint16_t); // Byte offset into index buffer GPU VA
	geometryTriangles.indexFormat = nvrhi::Format::R16_UINT;
	geometryTriangles.indexCount = indexCount;

	geometryTriangles.vertexBuffer = vertexBuffer;
	geometryTriangles.vertexOffset = 0;
	geometryTriangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
	geometryTriangles.vertexStride = vertexStride;
	geometryTriangles.vertexCount = vertexCount;

	geometryDesc.setTransform(Constants::kIdentityTransform.f);

	return geometryDesc;
}

bool BaseMesh::SetHidden(bool hidden)
{
	const bool wasHidden = m_State.any(State::Hidden);

	m_State.set(hidden, State::Hidden);

	if (wasHidden != hidden) {
		MarkDirty(DirtyFlags::Visibility);
		return true;
	}

	return false;
}

bool BaseMesh::IsTwoSided()
{
	return m_Properties.GetData().ShaderFlags & Properties::ShaderFlags::kTwoSided;
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

void BaseMesh::CreateMaterial()
{
	m_Material = Scene::GetSingleton()->GetSceneGraph()->GetMaterial(m_BSTriShape->GetGeometryRuntimeData().shaderProperty->material);
}

void BaseMesh::UpdateMaterial()
{
	if (!m_Material)
		return;

	// Only update water for now, saves some precious CPU time which we cannot afford (yet)
	if (m_Material->GetData()->Type != MaterialBase::Type::Water)
		return;

	m_Material->Update(m_BSTriShape->GetGeometryRuntimeData().shaderProperty->material);
}