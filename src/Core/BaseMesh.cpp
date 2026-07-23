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

BaseMesh::~BaseMesh()
{
	auto& meshManager = Scene::GetSingleton()->GetSceneGraph()->GetMeshManager();

	for (const auto geoIdx : m_GeometryIndex)
		meshManager->ReleaseGeometryIndex(geoIdx);

	if (m_MeshIndex != UINT16_MAX)
		meshManager->ReleaseMeshIndex(m_MeshIndex);
}

eastl::unique_ptr<BaseMesh> BaseMesh::Create(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
{
	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();

	if (geometryData.rendererData) {
		if (auto* extra = bsTriShape->GetExtraData<RE::NiIntegersExtraData>(Constants::ExtraData::LandLOD)) {
			if (extra->size > 0 && extra->value[0] == 4)
				return eastl::make_unique<LandLODMesh>(bsTriShape, commandList);
		}

		if (auto* subIndexTriShape = Util::Adapter::AsSubIndexTriShape(bsTriShape))
			return eastl::make_unique<SubIndexMesh>(subIndexTriShape);

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

void BaseMesh::WriteMeshData(eastl::vector<MeshData>& meshData) const
{
	using namespace DirectX;

	const uint16_t vertexID = GetVertexID();

	const auto& descs = GetGeometryDescs();
	for (size_t i = 0; i < descs.size(); i++) {
		auto& geomTris = descs[i].geometryData.triangles;

		meshData.emplace_back(
			GetIndexID(i),
			vertexID,
			VertexDesc(GetVertexDescRaw()),
			static_cast<uint16_t>(geomTris.vertexCount),
			static_cast<uint16_t>(geomTris.indexCount / 3),
			static_cast<uint16_t>(m_Type),
			static_cast<uint16_t>(GetDynamicIndex()),
			m_MeshIndex,
			static_cast<uint16_t>(geomTris.indexOffset / (sizeof(uint16_t) * 3)),
			m_Material->GetOffsetComp()
		);
	}
}

void BaseMesh::MarkDirty(DirtyFlags flag) {
	if (flag == DirtyFlags::None)
		return;

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
	m_Properties.Update(m_BSTriShape, m_Flags.all(Flags::Eyes));
	WriteProperties();

	m_WorldBound = m_BSTriShape->worldBound;

	// Update Transform
	{
		float3x4 transform;
		XMStoreFloat3x4(&transform, Util::Math::GetXMFromNiTransform(m_BSTriShape->world));

		if (m_NeedsPrevInit)
			MarkDirty(DirtyFlags::Transform);
		else if (!Util::Math::MatrixNearEqual(transform, m_Transform))
			MarkDirty(DirtyFlags::Transform);
		else if (!Util::Math::MatrixNearEqual(m_Transform, m_PrevTransform))
			MarkDirty(DirtyFlags::Transform);

		if (m_NeedsPrevInit) {
			m_PrevTransform = transform;
			m_NeedsPrevInit = false;
		}
		else {
			m_PrevTransform = m_Transform;
		}

		m_Transform = transform;
	}

	WriteTransform();

	// Update Geometry Desc opaque flag
	{
		const bool prevAlpha = m_Flags.all(Flags::Alpha);
		const bool alpha = m_Properties.IsAlpha();
		if (prevAlpha != alpha)
		{
			m_Flags.set(alpha, Flags::Alpha);

			for (auto& desc: m_GeometryDescs)
			{
				desc.flags = alpha ? nvrhi::rt::GeometryFlags::None : nvrhi::rt::GeometryFlags::Opaque;
			}

			MarkDirty(DirtyFlags::Alpha);
		}
	}

	UpdateMaterial();
}

void BaseMesh::PostUpdate()
{
	// SubIndexMesh has no cluster
	if (m_Cluster)
		m_Cluster->UpdateDirtyFlags(m_DirtyFlags.get());

	// Clear dirty flags after they've been "consumed" by the cluster
	ClearDirtyFlags();
}

nvrhi::rt::GeometryDesc BaseMesh::MakeGeometryDesc(nvrhi::IBuffer* indexBuffer, uint32_t indexOffset, uint32_t indexCount, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, uint32_t vertexCount, uint32_t transformIndex)
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

	if (transformIndex == UINT32_MAX)
		logger::critical("Mesh has unitialized transform index");

	geometryDesc.setTransformBuffer(
		Scene::GetSingleton()->GetSceneGraph()->GetTransformBuffer(),
		transformIndex * sizeof(TransformData));

	geometryDesc.flags = nvrhi::rt::GeometryFlags::Opaque;

	return geometryDesc;
}

void BaseMesh::SetHidden(bool hidden)
{
	const bool wasHidden = m_State.any(State::Hidden);

	m_State.set(hidden, State::Hidden);

	if (wasHidden != hidden)
		MarkDirty(DirtyFlags::Visibility);
}

bool BaseMesh::IsTwoSided()
{
	return m_Properties.GetData().ShaderFlags & Properties::ShaderFlags::kTwoSided;
}

bool BaseMesh::IsHidden() const
{
	return m_State.any(State::Hidden, State::SubIndexHidden);
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
	
	SetEyeFlag();

	return true;
}

void BaseMesh::SetEyeFlag()
{
	if (!m_Owner)
		return;

	// Once an eye, always an eye.
	if (!m_Flags.none(Flags::Eyes))
		return;

	auto baseObj = m_Owner->GetBaseObject();
	if (!baseObj)
		return;

	auto npc = baseObj->As<RE::TESNPC>();
	if (!npc)
		return;

	auto eyePart = npc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kEyes);
	if (!eyePart)
		return;

	const bool isEye = (strcmp(eyePart->formEditorID.c_str(), m_Name.c_str()) == 0);
	m_Flags.set(isEye, Flags::Eyes);
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

void BaseMesh::AllocateMeshIndex()
{
	m_MeshIndex = static_cast<uint16_t>(Scene::GetSingleton()->GetSceneGraph()->AllocateMeshIndex());
}

uint16_t BaseMesh::AllocateGeometryIndex()
{
	return static_cast<uint16_t>(Scene::GetSingleton()->GetSceneGraph()->AllocateGeometryIndex());
}


void BaseMesh::WriteProperties() const
{
	const auto& sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	sceneGraph->GetMeshManager()->WritePropertiesData(m_MeshIndex, m_Properties.GetData());
}

void BaseMesh::WriteTransform() const
{
	const auto& sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	sceneGraph->WriteTransformData(m_MeshIndex, m_Transform, m_PrevTransform);
}