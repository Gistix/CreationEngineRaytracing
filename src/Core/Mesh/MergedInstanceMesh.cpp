#include "Core/Mesh/MergedInstanceMesh.h"
#include "Core/MeshManager.h"
#include "Pass/Raytracing/Common/VertexAnimation.h"
#include "Renderer/RenderNode.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"
#include "Types.h"
#include "Types/RE/RE.h"

using namespace DirectX;

namespace
{
	DirectX::XMMATRIX BuildInstanceMatrix(const DirectX::XMMATRIX& a_shapeLocal, const RE::GrassInstanceData& a_record, bool a_uniformScale)
	{
		const float heightScale = static_cast<float>(a_record.heightScale);
		const float3 scaleMask = a_uniformScale ? float3(1.0f, 1.0f, 1.0f) : float3(0.0f, 0.0f, 1.0f);
		const float3 axisScale = heightScale * scaleMask + float3(1.0f, 1.0f, 1.0f);

		const float3 row0(a_record.rot1.x, a_record.rot1.y, a_record.rot1.z);
		const float3 row1(a_record.rot2.y, a_record.rot2.z, a_record.rot3.x);
		const float3 row2(a_record.rot3.z, a_record.rot2.x, a_record.rot3.y);
		const float3 translation(a_record.position.x, a_record.position.y, a_record.position.z);

		float3x4 instanceTransform(
			row0.x * axisScale.x, row0.y * axisScale.y, row0.z * axisScale.z, translation.x,
			row1.x * axisScale.x, row1.y * axisScale.y, row1.z * axisScale.z, translation.y,
			row2.x * axisScale.x, row2.y * axisScale.y, row2.z * axisScale.z, translation.z);

		// XMLoadFloat3x4 and GetXMFromNiTransform load transposed affine matrices (translation in r[3]),
		// so applying instanceTransform first and a_shapeLocal second is XMMatrixMultiply(instance, shapeLocal).
		return XMMatrixMultiply(XMLoadFloat3x4(&instanceTransform), a_shapeLocal);
	}
}

MergedInstanceMesh::MergedInstanceMesh(
	RE::BSTriShape* a_sourceShape,
	const eastl::vector<RE::GrassInstanceData>& a_instances,
	bool a_uniformScale,
	[[maybe_unused]] nvrhi::ICommandList* a_commandList)
{
	m_Name = MakeDebugName(a_sourceShape);
	m_BSTriShape = a_sourceShape;
	m_Type = Type::MergedInstance;

	const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(a_sourceShape);
	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("MergedInstanceMesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(a_sourceShape);
	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount))
		return;

	if (a_instances.empty())
		return;

	m_VertexDesc = rendererData->vertexDesc;

	m_IndexBuffer = CreateIndexBuffer(rendererData);
	m_VertexBuffer = CreateVertexBuffer(rendererData);
	if (!m_IndexBuffer.m_Buffer || !m_VertexBuffer.m_Buffer) {
		logger::warn("MergedInstanceMesh - No GPU index/vertex buffer for {}", m_Name);
		return;
	}

	const uint32_t indexCount = static_cast<uint32_t>(triShapeData.triangleCount) * 3;
	const uint16_t stride = Util::Geometry::GetStoredVertexSize(m_VertexDesc);
	const nvrhi::Format vertexFormat = Util::Geometry::GetVertexPositionFormat(m_VertexDesc);

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	auto* meshManager = sceneGraph->GetMeshManager().get();
	auto* instanceTransformBuffer = meshManager->GetInstanceTransformBuffer();

	const uint32_t count = static_cast<uint32_t>(a_instances.size());
	const uint32_t transformBase = meshManager->AllocateInstanceTransforms(count);
	if (transformBase == UINT32_MAX) {
		logger::warn("MergedInstanceMesh - Instance transform pool exhausted for {} ({} instances)", m_Name, count);
		return;
	}

	const auto shapeLocal = Util::Math::GetXMFromNiTransform(a_sourceShape->local);

	float3 boundMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
	float3 boundMax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());

	for (uint32_t i = 0; i < count; ++i)
	{
		const auto world = BuildInstanceMatrix(shapeLocal, a_instances[i], a_uniformScale);

		float3x4 xf;
		XMStoreFloat3x4(&xf, world);
		meshManager->WriteInstanceTransform(transformBase + i, xf, xf);

		const float3 fp(xf._14, xf._24, xf._34);
		boundMin = float3(std::min(boundMin.x, fp.x), std::min(boundMin.y, fp.y), std::min(boundMin.z, fp.z));
		boundMax = float3(std::max(boundMax.x, fp.x), std::max(boundMax.y, fp.y), std::max(boundMax.z, fp.z));
	}

	m_InstanceTransformBase = transformBase;
	m_InstanceCount = count;

	// All blade geometries share the source index/vertex buffers and a single mesh slot.
	if (!AllocateMeshIndex()) {
		logger::warn("MergedInstanceMesh - Mesh slot pool exhausted for {}", m_Name);
		return;
	}

	const uint16_t geometryIndex = AllocateGeometryIndex();
	if (geometryIndex == UINT16_MAX) {
		logger::warn("MergedInstanceMesh - Geometry slot pool exhausted for {}", m_Name);
		return;
	}

	m_GeometryEntries.reserve(count);
	for (uint32_t i = 0; i < count; ++i)
	{
		GeometryEntry entry;
		entry.desc = MakeGeometryDesc(
			m_IndexBuffer.m_Buffer, m_IndexBuffer.m_Offset, indexCount,
			m_VertexBuffer.m_Buffer, m_VertexBuffer.m_Offset, stride, triShapeData.vertexCount,
			instanceTransformBuffer, static_cast<uint64_t>(transformBase + i) * sizeof(TransformData),
			vertexFormat);
		entry.geometryIndex = (i == 0) ? geometryIndex : UINT16_MAX;
		m_GeometryEntries.push_back(entry);
	}

	m_VertexCount = triShapeData.vertexCount;
	if (!CreateWindBuffers(a_sourceShape, triShapeData.vertexCount, stride)) {
		logger::warn("MergedInstanceMesh - Failed to create grass wind buffers for {}", m_Name);
		return;
	}

	CreateMaterial();

	// Pad the bounds by the blade radius (the group AABB is only used for culling).
	const float bladeRadius = Util::Adapter::GetNiBoundRadius(a_sourceShape->worldBound);
	m_WorldBound.center = RE::NiPoint3(
		(boundMin.x + boundMax.x) * 0.5f,
		(boundMin.y + boundMax.y) * 0.5f,
		(boundMin.z + boundMax.z) * 0.5f);
	const float3 extent = boundMax - boundMin;
	m_WorldBound.radius = std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z) * 0.5f + bladeRadius;

	m_Transform = Constants::kIdentityTransform;
	m_PrevTransform = Constants::kIdentityTransform;
	m_World = RE::NiTransform();
	m_NeedsPrevInit = false;
	m_IsReady = m_Material != nullptr;
}

MergedInstanceMesh::~MergedInstanceMesh()
{
	if (m_InstanceTransformBase != UINT32_MAX)
		Scene::GetSingleton()->GetSceneGraph()->GetMeshManager()->ReleaseInstanceTransforms(m_InstanceTransformBase, m_InstanceCount);
}

bool MergedInstanceMesh::CreateWindBuffers(RE::BSTriShape* a_sourceShape, uint32_t a_vertexCount, uint16_t a_vertexStride)
{
	auto device = Renderer::GetSingleton()->GetDevice();
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	const uint64_t positionsPerBlock = static_cast<uint64_t>(m_InstanceCount) * a_vertexCount;

	auto windBufferDesc = nvrhi::BufferDesc()
		.setByteSize(2ull * positionsPerBlock * sizeof(float4))
		.setStructStride(sizeof(float4))
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} - Grass Wind Positions", m_Name.c_str()));

	m_WindPositionBuffer = device->createBuffer(windBufferDesc);
	if (!m_WindPositionBuffer)
		return false;

	// Dynamic slot: the read entry is unused for grass, the live SRV feeds shading, the UAV feeds VertexAnimation.
	m_DynamicDescriptor = sceneGraph->GetDynamicVertexReadDescriptors()->m_DescriptorTable->CreateDescriptorHandle(
		nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_WindPositionBuffer));
	if (!m_DynamicDescriptor.IsValid()) {
		m_WindPositionBuffer = nullptr;
		return false;
	}

	device->writeDescriptorTable(sceneGraph->GetDynamicVertexWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_UAV(m_DynamicDescriptor.Get(), m_WindPositionBuffer));
	device->writeDescriptorTable(sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_SRV(m_DynamicDescriptor.Get(), m_WindPositionBuffer));

	// VertexAnimation reads the native blade vertices (position + color) through the copy table.
	const uint32_t vertexSlot = m_VertexBuffer.m_Descriptor.Get();
	const uint64_t nativeBytes = static_cast<uint64_t>(a_vertexCount) * a_vertexStride;
	device->writeDescriptorTable(sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_SRV(vertexSlot, m_VertexBuffer.m_Buffer, nvrhi::BufferRange(m_VertexBuffer.m_Offset, nativeBytes)));

	// Each blade geometry reads its own block of swayed positions (float4 stride, xyz used).
	for (size_t i = 0; i < m_GeometryEntries.size(); ++i) {
		auto& triangles = m_GeometryEntries[i].desc.geometryData.triangles;
		triangles.vertexBuffer = m_WindPositionBuffer;
		triangles.vertexOffset = static_cast<uint64_t>(i) * a_vertexCount * sizeof(float4);
		triangles.vertexStride = sizeof(float4);
		triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
	}

	XMStoreFloat3x4(&m_InverseShapeLocal, Util::Math::GetXMFromNiTransform(a_sourceShape->local.Invert()));
	return true;
}

uint16_t MergedInstanceMesh::GetMeshDataFlags() const
{
	return m_WindPositionBuffer ? static_cast<uint16_t>(MeshDataFlags::WindPositions) : 0;
}

void MergedInstanceMesh::UpdateWind()
{
#if defined(SKYRIM)
	if (!m_WindPositionBuffer || !m_BSTriShape)
		return;

	auto* animationPass = Renderer::GetSingleton()->GetRenderGraph()->GetPass<Pass::VertexAnimation>();
	if (!animationPass)
		return;

	const auto wind = Util::Adapter::GetGrassWindParams(Util::Adapter::GetGeometryRuntimeData(m_BSTriShape).shaderProperty);

	// Raster fades grass out past fGrassStartFadeDistance + fGrassFadeRange; skip the wind (and the refit) there.
	const auto& iniSettings = Scene::GetSingleton()->m_INISettings;
	const RE::NiPoint3 cameraPosition = Util::Adapter::GetCameraEyePosition();
	const float boundDistance = cameraPosition.GetDistance(m_WorldBound.center) - m_WorldBound.radius;
	const bool inRange = wind.strength > 0.0f &&
		boundDistance < iniSettings.grassStartFadeDistance + iniSettings.grassFadeRange;

	// Leaving range still needs one evaluation (zero wind) so the positions settle back to the rest pose.
	if (!inRange && !m_WindActive && !m_NeedsWindInit)
		return;

	const float prevTimer = m_NeedsWindInit ? wind.timer : m_PrevWindTimer;
	m_PrevWindTimer = wind.timer;
	m_WindActive = inRange;
	m_NeedsWindInit = false;

	// Vanilla WindVector.xy: world +Y in the shape's model space (inverse world rotation), z dropped.
	const auto& world = m_BSTriShape->world;
	RE::NiPoint3 windDirection = world.rotate.Transpose() * RE::NiPoint3(0.0f, 1.0f, 0.0f);
	windDirection.Unitize();
	windDirection.z = 0.0f;

	// Displacement happens before the shape-local transform; express it in the instance pool's output space.
	const auto& local = m_BSTriShape->local;
	const RE::NiPoint3 windPool = (local.rotate * windDirection) * local.scale;

	GrassWindData data{};
	data.Index = m_VertexBuffer.m_Descriptor.Get();
	data.DynamicIndex = m_DynamicDescriptor.Get();
	data.VertexCount = m_VertexCount;
	data.InstanceCount = m_InstanceCount;
	data.InstanceTransformBase = m_InstanceTransformBase;

	const uint64_t vertexDescRaw = GetVertexDescRaw();
	std::memcpy(&data.VertexDesc, &vertexDescRaw, sizeof(uint64_t));

	data.Wind = float4(windPool.x, windPool.y, windPool.z, inRange ? wind.strength : 0.0f);
	data.Timers = float4(wind.timer, inRange ? prevTimer : wind.timer, 0.0f, 0.0f);
	data.InverseShapeLocal = m_InverseShapeLocal;

	animationPass->QueueGrassUpdate(this, data);

	MarkDirty(DirtyFlags::Vertex);
#endif
}

void MergedInstanceMesh::CollectOwnedBuffers(OwnedBufferStats& stats) const
{
	if (!m_WindPositionBuffer)
		return;

	stats.grassWindMeshCount++;
	stats.grassWindPositionBytes += m_WindPositionBuffer->getDesc().byteSize;
}

void MergedInstanceMesh::Update([[maybe_unused]] nvrhi::ICommandList* a_commandList)
{
	if (!m_BSTriShape)
		return;

	m_Properties.Update(m_BSTriShape, m_Flags.all(Flags::Eyes));
	WriteProperties();

	// Per-blade transforms live in the grass transform pool; the mesh transform stays identity.
	m_World = RE::NiTransform();
	m_Transform = Constants::kIdentityTransform;
	m_PrevTransform = Constants::kIdentityTransform;
	m_NeedsPrevInit = false;

	WriteTransform();

	const bool prevAlpha = m_Flags.all(Flags::Alpha);
	const bool alpha = m_Properties.IsAlpha();
	if (prevAlpha != alpha)
	{
		m_Flags.set(alpha, Flags::Alpha);

		for (auto& entry : m_GeometryEntries)
			entry.desc.flags = alpha ? nvrhi::rt::GeometryFlags::None : nvrhi::rt::GeometryFlags::Opaque;

		MarkDirty(DirtyFlags::Alpha);
	}

	UpdateMaterial();
}
