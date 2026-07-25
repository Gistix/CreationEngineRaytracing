#include "Core/BLASCluster.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"
#include "Types/InstanceMask.h"

#include <eastl/algorithm.h>

BLASCluster::BLASCluster(RE::TESObjectREFR* owner) :
	m_Owner(owner)
{
	if (m_Owner)
		m_Name = { std::format("Cluster {:08X}", m_Owner->GetFormID()).c_str() };
	else
		m_Name = { "Cluster (orphan)" };

	m_Flags.set(owner && Util::IsPlayer(owner), Flags::Player);
}

void BLASCluster::AddMember(BaseMesh* mesh)
{
	std::scoped_lock lock(m_MemberMutex);
	auto [it, inserted] = m_MemberSet.emplace(mesh);
	if (!inserted)
		return;

	m_Members.push_back(mesh);

	mesh->SetCluster(this);
	m_DirtyFlags.set(DirtyFlags::Mesh);
}

void BLASCluster::RemoveMember(BaseMesh* mesh)
{
	std::scoped_lock lock(m_MemberMutex);
	const bool removed = m_MemberSet.erase(mesh);
	if (!removed)
		return;

	m_Members.erase_last(mesh);

	mesh->SetCluster(nullptr);
	m_DirtyFlags.set(DirtyFlags::Mesh);
}

void BLASCluster::GrowBounds(const RE::NiBound& bound)
{
	float3 boundCenter = Util::Math::Float3(bound.center);

	float margin = m_ClusterRadius - bound.radius;
	if (margin > 0.0f) {
		float distSq = (boundCenter - m_ClusterPosition).LengthSquared();
		if (distSq <= margin * margin)
			return;
	}

	float distToBound = (boundCenter - m_ClusterPosition).Length();
	m_ClusterRadius = std::max(m_ClusterRadius, distToBound + bound.radius);
}

void BLASCluster::UpdateTransform() {

	if (m_Owner) {
		const RE::NiAVObject* object = m_Owner->Get3D();

		float3x4 transform;
		XMStoreFloat3x4(&transform, Util::Math::GetXMFromNiTransform(object->world));

		if (m_NeedsPrevInit) {
			m_PrevTransform = transform;
			m_NeedsPrevInit = false;
		} else {
			m_PrevTransform = m_Transform;
		}

		m_Transform = transform;
	}
	else {
		if (m_Members.empty()) {
			m_Transform = Constants::kIdentityTransform;
			m_PrevTransform = Constants::kIdentityTransform;
			m_NeedsPrevInit = false;
		}
		else {
			const auto& mesh = m_Members.front();
			m_Transform = mesh->GetTransform();

			if (m_NeedsPrevInit) {
				m_PrevTransform = m_Transform;
				m_NeedsPrevInit = false;
			} else {
				m_PrevTransform = mesh->GetPrevTransform();
			}
		}
	}

	m_ClusterPosition = float3(m_Transform._14, m_Transform._24, m_Transform._34);
	m_ClusterRadius = 0.0f;
}

bool BLASCluster::Empty() const
{
	return m_Members.empty();
}

bool BLASCluster::Valid() const
{
	return m_IsValid;
}

void BLASCluster::UpdateInstanceLightData(
    const eastl::map<RE::BSLight*, Light>& lights,
    const eastl::array<LightData, Constants::LIGHTS_MAX>& lightData)
{
	uint8_t lightIds[Constants::INSTANCE_LIGHTS_MAX];
	uint8_t numLights = 0;

	for (const auto& [bsLight, light] : lights) {
		if (!light.m_Active)
			continue;

		if (numLights >= Constants::INSTANCE_LIGHTS_MAX) {
			logger::error("BLASCluster::GetInstanceLightData - Number of lights per instance of {} exceeds the maximum of {}, for light {} of {}",
				numLights, 
				Constants::INSTANCE_LIGHTS_MAX, 
				light.m_Index,
				Constants::LIGHTS_MAX);
			break;
		}

		const auto& ld = lightData[light.m_Index];

		if (ld.Type == LightType::Directional) {
			lightIds[numLights] = light.m_Index;
			numLights++;
		} else {
			float dist = float3::Distance(m_ClusterPosition, ld.Vector);
			if (dist - m_ClusterRadius <= ld.Radius) {
				lightIds[numLights] = light.m_Index;
				numLights++;
			}
		}
	}

	m_InstanceLightData = InstanceLightData(lightIds, numLights);
}

void BLASCluster::UpdateDirtyFlags(const DirtyFlags& meshDirtyFlags)
{
	std::scoped_lock lock(m_DirtyMutex);
	m_DirtyFlags.set(meshDirtyFlags);
}

uint32_t BLASCluster::Update()
{
	UpdateTransform();

	auto scene = Scene::GetSingleton();
	const bool skipInstanceLights = scene->m_Settings.ExperimentalSettings.GlobalLights;

	// Only those who affect geometry count or its flags
	if (m_DirtyFlags.any(DirtyFlags::Visibility, DirtyFlags::Mesh, DirtyFlags::Alpha)) {
		m_Flags.reset(Flags::Updatable, Flags::TwoSided);

		m_GeometryDescs.clear();
		m_GeometrySlots.clear();

		auto* meshManager = scene->GetSceneGraph()->GetMeshManager().get();

		for (const auto& mesh : m_Members) {
			if (mesh->IsHidden())
				continue;

			if (!skipInstanceLights)
				GrowBounds(mesh->GetWorldBound());

			const auto& entries = mesh->GetGeometryEntries();
			if (entries.empty())
				continue;

			if (mesh->IsUpdatable())
				m_Flags.set(Flags::Updatable);

			if (mesh->IsTwoSided())
				m_Flags.set(Flags::TwoSided);

			const uint16_t vertexID = mesh->GetVertexID();
			const auto vertexDesc = VertexDesc(mesh->GetVertexDescRaw());
			const auto meshType = static_cast<uint16_t>(mesh->GetType());
			const auto dynamicIndex = static_cast<uint16_t>(mesh->GetDynamicIndex());
			const auto meshIndex = mesh->GetMeshIndex();
			const auto materialIndex = mesh->GetMaterial()->GetOffsetComp();

			for (size_t i = 0; i < entries.size(); i++) {
				const auto& entry = entries[i];
				m_GeometryDescs.push_back(entry.desc);
				m_GeometrySlots.push_back(entry.geometryIndex);

				auto& geomTris = entry.desc.geometryData.triangles;
				MeshData md(
					mesh->GetIndexID(i),
					vertexID,
					vertexDesc,
					static_cast<uint16_t>(geomTris.vertexCount),
					static_cast<uint16_t>(geomTris.indexCount / 3),
					meshType,
					dynamicIndex,
					meshIndex,
					static_cast<uint16_t>(geomTris.indexOffset / (sizeof(uint16_t) * 3)),
					materialIndex
				);
				meshManager->WriteMeshData(entry.geometryIndex, md);
			}
		}
	}

	const uint32_t meshCount = static_cast<uint32_t>(m_GeometrySlots.size());

	m_IsValid = meshCount > 0;
	if (m_IsValid && !skipInstanceLights) {
		auto sceneGraph = scene->GetSceneGraph();

		// TODO: Move this to the GPU - It doesn't scale well on CPU
		UpdateInstanceLightData(sceneGraph->GetLights(), sceneGraph->GetLightData());
	}

	return meshCount;
}

void BLASCluster::WriteInstanceData(uint32_t firstMesh, uint32_t meshCount, InstanceData& instanceData) const
{
	instanceData.Transform = m_Transform;
	instanceData.PrevTransform = m_PrevTransform;
	instanceData.LightData = m_InstanceLightData;
	instanceData.FirstGeometryID = firstMesh;
	instanceData.NumGeometry = meshCount;
	instanceData.Alpha = 1.0f;
}

nvrhi::rt::AccelStructDesc BLASCluster::MakeDesc(BuildMode mode) const
{
	auto blasDesc = nvrhi::rt::AccelStructDesc()
		.setIsTopLevel(false)
		.setDebugName(m_Name.c_str());

	// Updatable clusters favour fast builds (frequent refits); static clusters favour fast traversal.
	blasDesc.buildFlags = m_Flags.all(Flags::Updatable)
		? nvrhi::rt::AccelStructBuildFlags::PreferFastBuild
		: nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;

	blasDesc.buildFlags |= (mode == BuildMode::Update
		? nvrhi::rt::AccelStructBuildFlags::PerformUpdate
		: nvrhi::rt::AccelStructBuildFlags::AllowUpdate);

	return blasDesc;
}

BLASCluster::BuildMode BLASCluster::DetermineBuildMode(SceneGraph* sceneGraph, uint64_t frameIndex)
{
	const bool firstBuild = (m_LastBuildFrame == Constants::INVALID_FRAME_INDEX);
	const bool hasMesh = m_DirtyFlags.any(DirtyFlags::Mesh);
	const bool hasVisibility = m_DirtyFlags.any(DirtyFlags::Visibility);
	const bool hasUpdate = m_DirtyFlags.any(DirtyFlags::Vertex, DirtyFlags::Skin, DirtyFlags::Transform, DirtyFlags::Alpha);
	const bool isOrphan = (m_Owner == nullptr);

	if (firstBuild || !m_BLAS || hasMesh || (!isOrphan && hasVisibility))
		return BuildMode::Rebuild;

	if (hasUpdate) {
		if (m_UpdateCount >= Constants::MAX_BLAS_UPDATES_BEFORE_MAINTENANCE &&
			sceneGraph->TryMaintenanceRebuild(frameIndex))
			return BuildMode::Rebuild;

		return BuildMode::Update;
	}

	return BuildMode::Skip;
}

nvrhi::rt::InstanceDesc BLASCluster::MakeInstanceDesc() const
{
	auto instanceDesc = nvrhi::rt::InstanceDesc()
		.setInstanceID(m_InstanceIndex)
		.setInstanceMask(InstanceMask::Default)
		.setTransform(m_Transform.f)
		.setFlags(m_Flags.all(Flags::TwoSided) ? nvrhi::rt::InstanceFlags::TriangleCullDisable : nvrhi::rt::InstanceFlags::None)
		.setBLAS(m_BLAS);

	return instanceDesc;
}

BLASCluster::PrepareBuildResult BLASCluster::PrepareBuildUpdate(SceneGraph* sceneGraph)
{
	PrepareBuildResult prep;

	auto* renderer = Renderer::GetSingleton();
	auto* device = renderer->GetDevice();
	const auto frameIndex = renderer->GetFrameIndex();

	if (frameIndex == m_LastBuildFrame)
		return prep;

	prep.mode = DetermineBuildMode(sceneGraph, frameIndex);

	// Orphan clusters excluded from the TLAS while hidden: their BLAS remains valid and can be
	// reused when the mesh becomes visible again. Consume the visibility flag here so Issue can
	// remain a no-op for this path.
	if (prep.mode == BuildMode::Skip && m_Owner == nullptr && m_DirtyFlags == DirtyFlags::Visibility)
	{
		m_DirtyFlags.reset();
		m_LastBuildFrame = frameIndex;
		prep.mode = BuildMode::Skip;
		return prep;
	}

	if (prep.mode == BuildMode::Skip)
		return prep;

	if (prep.mode == BuildMode::Rebuild)
		m_UpdateCount = 0;
	else
		m_UpdateCount++;

	if (m_GeometryDescs.empty())
	{
		m_BLAS = nullptr;
		m_LastBuildFrame = frameIndex;
		m_DirtyFlags.reset();
		prep.mode = BuildMode::Skip;
		return prep;
	}

	prep.desc = MakeDesc(prep.mode);
	prep.desc.bottomLevelGeometries = m_GeometryDescs;

	// Allocate a new accel struct on first build or when the required size grows.
	prep.needsAllocation = !m_BLAS;
	if (!prep.needsAllocation && prep.mode == BuildMode::Rebuild)
	{
		// getAccelStructPreBuildInfo is a pure device query (no AddRef/mutation) and is safe to
		// call from worker threads; it computes worst-case BLAS size from the desc's geometry list.
		auto prebuildInfo = device->getAccelStructPreBuildInfo(prep.desc);
		prep.needsAllocation = prebuildInfo.resultMaxSizeInBytes > m_BLAS->getBufferSize();
	}

	return prep;
}

void BLASCluster::AllocateBLAS(const PrepareBuildResult& prep)
{
	if (prep.mode == BuildMode::Skip)
		return;

	if (prep.needsAllocation)
		m_BLAS = Renderer::GetSingleton()->GetDevice()->createAccelStruct(prep.desc);
}

void BLASCluster::RecordBuildUpdate(nvrhi::ICommandList* commandList, const PrepareBuildResult& prep)
{
	if (prep.mode == BuildMode::Skip)
		return;

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, m_BLAS, prep.desc);

	const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();
	m_DirtyFlags.reset();
	m_LastBuildFrame = frameIndex;
}