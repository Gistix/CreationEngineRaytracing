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

const eastl::vector<MeshData>& BLASCluster::Update()
{
	UpdateTransform();

	auto scene = Scene::GetSingleton();
	const bool skipInstanceLights = scene->m_Settings.ExperimentalSettings.GlobalLights;

	// Only those who affect geometry count or its flags
	if (m_DirtyFlags.any(DirtyFlags::Visibility, DirtyFlags::Mesh, DirtyFlags::Alpha)) {
		m_Flags.reset(Flags::Updatable, Flags::TwoSided);

		m_GeometryDescs.clear();
		m_MeshData.clear();
		m_MeshSlots.clear();
		m_GeometrySlots.clear();

		for (const auto& mesh : m_Members) {
			if (mesh->IsHidden())
				continue;

			if (!skipInstanceLights)
				GrowBounds(mesh->GetWorldBound());

			const auto& descs = mesh->GetGeometryDescs();
			if (descs.empty())
				continue;

			m_GeometryDescs.insert(m_GeometryDescs.end(), descs.begin(), descs.end());

			if (mesh->IsUpdatable())
				m_Flags.set(Flags::Updatable);

			if (mesh->IsTwoSided())
				m_Flags.set(Flags::TwoSided);

			const size_t before = m_MeshData.size();
			mesh->WriteMeshData(m_MeshData);
			for (size_t i = before; i < m_MeshData.size(); i++) {
				m_MeshSlots.push_back(mesh->GetMeshIndex());
				m_GeometrySlots.push_back(mesh->GetGeometryIndex(i - before));
			}
		}

		// Push each geometry's MeshData to its own geometry-indexed slot
		auto* meshManager = scene->GetSceneGraph()->GetMeshManager().get();
		for (size_t i = 0; i < m_GeometrySlots.size(); i++)
			meshManager->WriteMeshData(m_GeometrySlots[i], m_MeshData[i]);
	}

	m_IsValid = !m_MeshData.empty();
	if (m_IsValid && !skipInstanceLights) {
		auto sceneGraph = scene->GetSceneGraph();

		// TODO: Move this to the GPU - It doesn't scale well on CPU
		UpdateInstanceLightData(sceneGraph->GetLights(), sceneGraph->GetLightData());
	}

	return m_MeshData;
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

void BLASCluster::BuildUpdate(nvrhi::ICommandList* commandList, SceneGraph* sceneGraph)
{
	auto* renderer = Renderer::GetSingleton();
	auto* device = renderer->GetDevice();
	const auto frameIndex = renderer->GetFrameIndex();

	if (frameIndex == m_LastBuildFrame) {
		logger::info("BLASCluster::BuildUpdate - {} already built this frame, skipping", m_Name);
		return;
	}

	const auto buildMode = DetermineBuildMode(sceneGraph, frameIndex);
	if (buildMode == BuildMode::Skip && m_Owner == nullptr && m_DirtyFlags.any(DirtyFlags::Visibility)) {
		// Orphan clusters contain one mesh and are excluded from the TLAS while hidden.
		// Their BLAS remains valid and can be reused when the mesh becomes visible again.
		m_DirtyFlags.reset();
		m_LastBuildFrame = frameIndex;
		return;
	}
	if (buildMode == BuildMode::Skip) {
		eastl::string membersInfo;
		for (const auto* member : m_Members) {
			if (!membersInfo.empty())
				membersInfo += ", ";
			membersInfo += std::format("{} ({})", member->GetName().c_str(), magic_enum::enum_name(member->GetType())).c_str();
		}
		logger::info("BLASCluster::BuildUpdate - {}: {} with {} members and {} geometry descs has no dirty flags set. Members: [{}]",
			fmt::ptr(this), m_Name, m_Members.size(), m_GeometryDescs.size(), membersInfo);
		return;
	}

	if (buildMode == BuildMode::Rebuild)
		m_UpdateCount = 0;
	else
		m_UpdateCount++;

	if (m_GeometryDescs.empty()) {
		m_BLAS = nullptr;
		m_LastBuildFrame = frameIndex;
		m_DirtyFlags.reset();
		return;
	}

	// Allocate a new accel struct on first build or when the required size grows.
	const bool allocate = !m_BLAS;

	auto blasDesc = MakeDesc(buildMode);
	blasDesc.bottomLevelGeometries = m_GeometryDescs;

	bool needsAllocation = allocate;
	if (!needsAllocation && buildMode == BuildMode::Rebuild) {
		auto prebuildInfo = device->getAccelStructPreBuildInfo(blasDesc);
		needsAllocation = prebuildInfo.resultMaxSizeInBytes > m_BLAS->getBufferSize();
	}

	if (needsAllocation)
		m_BLAS = device->createAccelStruct(blasDesc);

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, m_BLAS, blasDesc);

	m_DirtyFlags.reset();
	m_LastBuildFrame = frameIndex;
}
