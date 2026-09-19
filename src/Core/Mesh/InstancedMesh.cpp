#include "Core/Mesh/InstancedMesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"
#include "Types.h"

InstancedMesh::InstancedMesh(RE::BSTriShape* bsTriShape, [[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(bsTriShape);
	m_BSTriShape = bsTriShape;
	m_Type = Type::Instanced;

	const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);
	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("InstancedMesh::InstancedMesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(bsTriShape);
	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount))
		return;

	m_VertexDesc = rendererData->vertexDesc;
	m_IndexBuffer = CreateIndexBuffer(rendererData);
	m_VertexBuffer = CreateVertexBuffer(rendererData);

	AllocateMeshIndex();

	const uint32_t indexCount = static_cast<uint32_t>(triShapeData.triangleCount) * 3;
	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(rendererData->vertexDesc);
	const nvrhi::Format vertexFormat = Util::Geometry::GetVertexPositionFormat(rendererData->vertexDesc);

	m_GeometryEntries.push_back({ MakeGeometryDesc(
		m_IndexBuffer.m_Buffer, m_IndexBuffer.m_Offset, indexCount,
		m_VertexBuffer.m_Buffer, m_VertexBuffer.m_Offset, vertexStride, triShapeData.vertexCount,
		GetMeshIndex(), vertexFormat), AllocateGeometryIndex() });

	CreateMaterial();

	// The engine's first AddGroup fires during block Attach, before this mesh is created. The
	// SceneGraph owns the parsed instance data; keep a stable pointer to it for the mesh's lifetime.
	m_InstancedData = Scene::GetSingleton()->GetSceneGraph()->GetOrCreateInstancedData(bsTriShape);
}

void InstancedMesh::OnDestroy()
{
	m_InstancedData = nullptr;
	BaseMesh::OnDestroy();
}

void InstancedMesh::RebuildInstances()
{
	if (!m_InstancedData)
		return;

	const auto& instances = m_InstancedData->instances;

	const uint32_t oldCount = static_cast<uint32_t>(m_InstanceData.size());
	const float3x4 oldFront = m_InstanceData.empty() ? Constants::kIdentityTransform : m_InstanceData.front().transform;

	m_InstanceData.clear();
	m_InstanceData.reserve(instances.size());

	for (const auto& instance : instances)
	{
		// The engine encodes hidden instances as zero scale.
		if (instance.scale == 0.0f || instance.alpha == 0.0f)
			continue;

		const float c = instance.cosZ;
		const float s = instance.sinZ;

		RE::NiMatrix3 rotate;
		rotate.entry[0][0] = c;   rotate.entry[0][1] = s;   rotate.entry[0][2] = 0.0f;
		rotate.entry[1][0] = -s;  rotate.entry[1][1] = c;   rotate.entry[1][2] = 0.0f;
		rotate.entry[2][0] = 0.0f; rotate.entry[2][1] = 0.0f; rotate.entry[2][2] = 1.0f;

		auto instanceTransform = RE::NiTransform();
		instanceTransform.rotate = rotate;
		instanceTransform.translate = RE::NiPoint3(instance.position.x, instance.position.y, instance.position.z);
		instanceTransform.scale = instance.scale;

		auto worldTransform = m_BSTriShape->local * instanceTransform;

		float3x4 xf;
		XMStoreFloat3x4(&xf, Util::Math::GetXMFromNiTransform(worldTransform));

		m_InstanceData.push_back({ xf, xf, instance.alpha });
	}

	const uint32_t newCount = static_cast<uint32_t>(m_InstanceData.size());
	if (newCount != oldCount)
		MarkDirty(DirtyFlags::Mesh);
	else if (!m_InstanceData.empty() && std::memcmp(&m_InstanceData.front().transform, &oldFront, sizeof(float3x4)) != 0)
		MarkDirty(DirtyFlags::Transform);
}

void InstancedMesh::Update([[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Properties.Update(m_BSTriShape, m_Flags.all(Flags::Eyes));
	WriteProperties();

	m_WorldBound = m_BSTriShape->worldBound;

	if (m_InstancedData && m_InstancedData->changed)
	{
		RebuildInstances();
		m_InstancedData->changed = false;
	}

	// Template mesh local transform: match first instance's transform so GPU TransformComposition
	// (InverseAffine(Instances[remapInstance].Transform) * CurrentTransforms[meshID]) evaluates to identity.
	if (!m_InstanceData.empty()) {
		m_Transform = m_InstanceData.front().transform;
		m_PrevTransform = m_InstanceData.front().prevTransform;
	} else {
		m_Transform = Constants::kIdentityTransform;
		m_PrevTransform = Constants::kIdentityTransform;
	}
	m_World = RE::NiTransform();
	m_NeedsPrevInit = false;

	WriteTransform();

	// Update geometry desc alpha / opaque flag
	const bool prevAlpha = m_Flags.all(Flags::Alpha);
	const bool alpha = m_Properties.IsAlpha();
	if (prevAlpha != alpha)
	{
		m_Flags.set(alpha, Flags::Alpha);

		for (auto& entry : m_GeometryEntries)
		{
			entry.desc.flags = alpha ? nvrhi::rt::GeometryFlags::None : nvrhi::rt::GeometryFlags::Opaque;
		}

		MarkDirty(DirtyFlags::Alpha);
	}

	UpdateMaterial();
}