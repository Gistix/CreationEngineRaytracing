#include "Core/Mesh/BaseMesh.h"
#include "Core/Mesh/Mesh.h"
#include "Core/Mesh/LandLODMesh.h"
#include "Core/Mesh/SkinnedMesh.h"
#include "Core/Mesh/DynamicMesh.h"
#include "Core/Mesh/SubIndexMesh.h"
#include "Core/Mesh/InstancedMesh.h"
#include "Renderer.h"
#include "Utils/DXVKInterop.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Types/RE/RE.h"
#include "interop/Triangle.hlsli"

BaseMesh::~BaseMesh()
{
	auto& meshManager = Scene::GetSingleton()->GetSceneGraph()->GetMeshManager();

	for (const auto& entry : m_GeometryEntries)
		meshManager->ReleaseGeometryIndex(entry.geometryIndex);

	if (m_MeshIndex != UINT16_MAX)
		meshManager->ReleaseMeshIndex(m_MeshIndex);
}

eastl::unique_ptr<BaseMesh> BaseMesh::Create(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
{
	const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);

	if (geometryData.rendererData) {
		if (auto* extra = Util::Adapter::GetIntegersExtraData(bsTriShape, Constants::ExtraData::LandLOD)) {
			if (extra->size > 0 && extra->value[0] == 4)
				return eastl::make_unique<LandLODMesh>(bsTriShape, commandList);
		}

		if (auto* subIndexTriShape = Util::Adapter::AsSubIndexTriShape(bsTriShape))
			return eastl::make_unique<SubIndexMesh>(subIndexTriShape);

		if (auto* multiStreamTriShape = Util::Adapter::AsMultiStreamInstanceTriShape(bsTriShape))
			return eastl::make_unique<InstancedMesh>(multiStreamTriShape, commandList);

#if defined(FALLOUT4)
		// Does this mean DynamicMesh has rendererData in Fallout4?
		// It would also apply for SkinnedMesh
		if (!geometryData.rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_VERTEX)) {
			logger::warn("BaseMesh::Create - Mesh {} has no vertex position.", MakeDebugName(bsTriShape).c_str());
			return nullptr;
		}
#endif

		return eastl::make_unique<Mesh>(bsTriShape, commandList);
	}

#if !defined(FALLOUT4)
	if (auto bsDynamicTriShape = Util::Adapter::AsDynamicTriShape(bsTriShape))
		return eastl::make_unique<DynamicMesh>(bsDynamicTriShape, commandList);
#endif

	if (geometryData.skinInstance)
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

void BaseMesh::MarkDirty(DirtyFlags flag) {
	if (flag == DirtyFlags::None)
		return;

	// Mesh-local flags reach the cluster via CommitDirtyFlags(), which BuildClusters scans.
	m_DirtyFlags.set(flag);
}

bool BaseMesh::ValidateCounts(uint32_t numTriangles, uint32_t numVertices)
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

BaseMesh::BufferDescriptor BaseMesh::CreateVulkanBuffer(
	ID3D11Buffer* buffer11,
	const char* debugName,
	const char* logContext,
	const char* resourceKind,
	DescriptorTableManager* descriptorTable)
{
	BufferDescriptor buffer{};

	if (!buffer11) {
		logger::error("{} - D3D11 {} buffer is null", logContext, resourceKind);
		return buffer;
	}

	winrt::com_ptr<IDXGIVkInteropBuffer> interopBuffer;
	auto hr = buffer11->QueryInterface(IID_PPV_ARGS(interopBuffer.put()));
	if (FAILED(hr)) {
		logger::error("{} - Failed to query IDXGIVkInteropBuffer: 0x{:08X}", logContext, static_cast<uint32_t>(hr));
		return buffer;
	}

	VkBuffer vkBuffer = VK_NULL_HANDLE;
	VkDeviceSize sliceOffset = 0;
	VkDeviceSize sliceLength = 0;
	VkDeviceAddress gpuAddress = 0;
	hr = interopBuffer->GetVulkanBufferInfo(&vkBuffer, &sliceOffset, &sliceLength, &gpuAddress);
	if (FAILED(hr) || !vkBuffer) {
		logger::error("{} - GetVulkanBufferInfo failed: 0x{:08X}", logContext, static_cast<uint32_t>(hr));
		return buffer;
	}

	auto bufferDesc = nvrhi::BufferDesc()
		.setByteSize(sliceOffset + sliceLength)
		.setCanHaveRawViews(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(debugName);

	auto device = Renderer::GetSingleton()->GetDevice();
	buffer.m_Buffer = device->createHandleForNativeBuffer(
		nvrhi::ObjectTypes::VK_Buffer,
		nvrhi::Object(vkBuffer),
		bufferDesc);

	if (buffer.m_Buffer) {
		buffer.m_Descriptor = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::RawBuffer_SRV(0, buffer.m_Buffer));
	}
	else {
		logger::error("{} - Failed to create handle for native Vulkan buffer", logContext);
	}

	buffer.m_Offset = sliceOffset;
	return buffer;
}

BaseMesh::BufferDescriptor BaseMesh::CreateDX12Buffer(
	ID3D12Resource* resourceDX12,
	ID3D11Buffer* buffer11,
	const char* debugName,
	const char* logContext,
	const char* resourceKind,
	DescriptorTableManager* descriptorTable,
	uint64_t offset)
{
	BufferDescriptor buffer{};

	auto resourceDesc = resourceDX12->GetDesc();

	D3D11_BUFFER_DESC resourceDesc11;
	buffer11->GetDesc(&resourceDesc11);

	if (resourceDesc.Width != resourceDesc11.ByteWidth) {
		logger::error("D3D11 ({}) and D3D12 ({}) {} buffer size mismatch.", resourceDesc11.ByteWidth, resourceDesc.Width, resourceKind);
		return buffer;
	}

	auto bufferDesc = nvrhi::BufferDesc()
		.setByteSize(resourceDesc.Width)
		.setCanHaveRawViews(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(debugName);

	auto device = Renderer::GetSingleton()->GetDevice();
	buffer.m_Buffer = device->createHandleForNativeBuffer(
		nvrhi::ObjectTypes::D3D12_Resource,
		nvrhi::Object(resourceDX12),
		bufferDesc);

	if (buffer.m_Buffer) {
		buffer.m_Descriptor = descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::RawBuffer_SRV(0, buffer.m_Buffer));
	}
	else {
		logger::error("{} - Failed to create handle for native buffer;", logContext);
	}

	buffer.m_Offset = offset;
	return buffer;
}

BaseMesh::BufferDescriptor BaseMesh::CreateIndexBuffer(RE::BSGraphics::TriShape* triShape)
{
	auto* descriptorTable = Scene::GetSingleton()->GetSceneGraph()->GetTriangleDescriptors()->m_DescriptorTable.get();

	if (Renderer::GetSingleton()->IsVulkan()) {
		return CreateVulkanBuffer(
			Util::Adapter::GetD3D11IndexBuffer(triShape),
			"Index Buffer (VK)",
			"BaseMesh::CreateIndexBuffer",
			"index",
			descriptorTable);
	}

#if defined(FALLOUT4)
	const uint64_t offset = triShape->indexBuffer->dataOffset;
#else
	const uint64_t offset = 0;
#endif

	return CreateDX12Buffer(
		Util::Adapter::GetIndexBufferDX12(triShape),
		Util::Adapter::GetD3D11IndexBuffer(triShape),
		"Index Buffer",
		"BaseMesh::CreateIndexBuffer",
		"index",
		descriptorTable,
		offset);
}

BaseMesh::BufferDescriptor BaseMesh::CreateVertexBuffer(RE::BSGraphics::TriShape* triShape)
{
	auto* descriptorTable = Scene::GetSingleton()->GetSceneGraph()->GetVertexDescriptors()->m_DescriptorTable.get();

	if (Renderer::GetSingleton()->IsVulkan()) {
		return CreateVulkanBuffer(
			Util::Adapter::GetD3D11VertexBuffer(triShape),
			"Vertex Buffer (VK)",
			"BaseMesh::CreateVertexBuffer",
			"vertex",
			descriptorTable);
	}

#if defined(FALLOUT4)
	const uint64_t offset = triShape->vertexBuffer->dataOffset;
#else
	const uint64_t offset = 0;
#endif

	return CreateDX12Buffer(
		Util::Adapter::GetVertexBufferDX12(triShape),
		Util::Adapter::GetD3D11VertexBuffer(triShape),
		"Vertex Buffer",
		"BaseMesh::CreateVertexBuffer",
		"vertex",
		descriptorTable,
		offset);
}

void BaseMesh::Update([[ maybe_unused ]] nvrhi::ICommandList* commandList)
{ 
	m_Properties.Update(m_BSTriShape, m_Flags.all(Flags::Eyes));
	WriteProperties();

	m_WorldBound = m_BSTriShape->worldBound;

	// Update Transform
	{
		m_World = m_BSTriShape->world;

		const bool isPlayer = m_Cluster ? m_Cluster->IsPlayer() : false;
		bool drawFirstPerson = false;
		if (isPlayer) {
			const auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
			if ((drawFirstPerson = sceneGraph->GetDrawFirstPerson()))
				m_World.translate += sceneGraph->GetFirstPersonPosition();
		}

		m_Flags.set(drawFirstPerson, Flags::FirstPerson);

		float3x4 transform;
		XMStoreFloat3x4(&transform, Util::Math::GetXMFromNiTransform(m_World));

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

		WriteTransform();
	}

	// Update Geometry Desc opaque flag
	{
		const bool prevAlpha = m_Flags.all(Flags::Alpha);
		const bool alpha = m_Properties.IsAlpha();
		if (prevAlpha != alpha)
		{
			m_Flags.set(alpha, Flags::Alpha);

			for (auto& entry: m_GeometryEntries)
			{
				entry.desc.flags = alpha ? nvrhi::rt::GeometryFlags::None : nvrhi::rt::GeometryFlags::Opaque;
			}

			MarkDirty(DirtyFlags::Alpha);
		}
	}

	UpdateMaterial();
}

void BaseMesh::CommitDirtyFlags()
{
	// SubIndexMesh has no cluster
	if (m_Cluster)
		m_Cluster->UpdateDirtyFlags(m_DirtyFlags.get());

	// Clear dirty flags after they've been "consumed" by the cluster
	ClearDirtyFlags();
}

nvrhi::rt::GeometryDesc BaseMesh::MakeGeometryDesc(nvrhi::IBuffer* indexBuffer, uint64_t indexOffset, uint32_t indexCount, nvrhi::IBuffer* vertexBuffer, uint64_t vertexOffset, uint16_t vertexStride, uint32_t vertexCount, uint32_t transformIndex, nvrhi::Format vertexFormat)
{
	nvrhi::rt::GeometryDesc geometryDesc;

	auto& geometryTriangles = geometryDesc.geometryData.triangles;

	geometryTriangles.indexBuffer = indexBuffer;
	geometryTriangles.indexOffset = indexOffset; // Byte offset into index buffer GPU VA
	geometryTriangles.indexFormat = nvrhi::Format::R16_UINT;
	geometryTriangles.indexCount = indexCount;

	geometryTriangles.vertexBuffer = vertexBuffer;
	geometryTriangles.vertexOffset = vertexOffset; // Byte offset into vertex buffer GPU VA
	geometryTriangles.vertexFormat = vertexFormat;
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

#if defined(SKYRIM)
	auto baseObj = Util::Adapter::GetBaseObject(m_Owner);
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
#endif
}

void BaseMesh::CreateMaterial()
{
	auto shaderProperty = Util::Adapter::GetGeometryRuntimeData(m_BSTriShape).shaderProperty;
	m_Material = Scene::GetSingleton()->GetSceneGraph()->GetMaterial(shaderProperty);
}

void BaseMesh::UpdateMaterial()
{
	if (!m_Material)
		return;

	// Only update water for now, saves some precious CPU time which we cannot afford (yet)
	if (m_Material->GetData()->Type != MaterialBase::Type::Water)
		return;

	m_Material->Update(Util::Adapter::GetGeometryRuntimeData(m_BSTriShape).shaderProperty->material);
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