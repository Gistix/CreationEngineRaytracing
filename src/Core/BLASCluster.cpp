#include "Core/BLASCluster.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"
#include "Types/InstanceMask.h"

#include <eastl/algorithm.h>

BLASCluster::BLASCluster(RE::TESObjectREFR* owner, const char* customName) :
	m_Owner(owner)
{
	if (customName)
		m_Name = customName;
	else if (m_Owner)
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

void BLASCluster::UpdateTransform() {

	if (m_Owner) {
		auto* object = m_Owner->Get3D(false);
		auto world = object->world;

		// Mental gymnastics here because the first person node is placed at origin and only properly translated during first person view rendering
		if (IsPlayer()) {
			const auto sceneGraph = Scene::GetSingleton()->GetSceneGraph();
			if (sceneGraph->GetDrawFirstPerson()) {
				object = Util::Adapter::GetFirstPerson3D(RE::PlayerCharacter::GetSingleton());
				world = object->world;
				world.translate += sceneGraph->GetFirstPersonPosition();
			}
		}

		float3x4 transform;
		XMStoreFloat3x4(&transform, Util::Math::GetXMFromNiTransform(world));

		if (m_NeedsPrevInit) {
			m_PrevTransform = transform;
			m_NeedsPrevInit = false;
		} else {
			m_PrevTransform = m_Transform;
		}

		m_Transform = transform;

		m_WorldBound = object->worldBound;

		AABB clusterAABB;
		for (const auto* mesh : m_Members) {
			if (!mesh->IsHidden())
				clusterAABB.Union(mesh->GetWorldAABB());
		}
		m_WorldAABB = clusterAABB;
	}
	else {
		if (m_Members.empty()) {
			m_Transform = Constants::kIdentityTransform;
			m_PrevTransform = Constants::kIdentityTransform;
			m_NeedsPrevInit = false;
			Util::Adapter::SetNiBound(m_WorldBound, { 0.0f, 0.0f, 0.0f }, 0.0f);
			m_WorldAABB = AABB();
		}
		else if (m_Members.size() == 1) {
			const auto& mesh = m_Members.front();
			m_Transform = mesh->GetTransform();

			if (m_NeedsPrevInit) {
				m_PrevTransform = m_Transform;
				m_NeedsPrevInit = false;
			} else {
				m_PrevTransform = mesh->GetPrevTransform();
			}

			m_WorldBound = mesh->GetWorldBound();
			m_WorldAABB = mesh->GetWorldAABB();
		}
		else {
			// Multi-member spatial cluster: instance transform is Identity
			m_Transform = Constants::kIdentityTransform;
			m_PrevTransform = Constants::kIdentityTransform;
			m_NeedsPrevInit = false;

			AABB clusterAABB;
			bool anyVisible = false;

			for (const auto* mesh : m_Members) {
				if (mesh->IsHidden())
					continue;

				clusterAABB.Union(mesh->GetWorldAABB());
				anyVisible = true;
			}

			m_WorldAABB = clusterAABB;

			if (anyVisible && clusterAABB.IsValid()) {
				const float3 center = clusterAABB.GetCenter();
				const float3 halfExt = clusterAABB.GetHalfExtents();
				const float radius = std::sqrt(halfExt.x * halfExt.x + halfExt.y * halfExt.y + halfExt.z * halfExt.z);
				Util::Adapter::SetNiBound(m_WorldBound, RE::NiPoint3(center.x, center.y, center.z), radius);
			} else {
				Util::Adapter::SetNiBound(m_WorldBound, { 0.0f, 0.0f, 0.0f }, 0.0f);
			}
		}
	}
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
		}
		else {
			const auto& center = Util::Math::Float3(m_WorldBound.center);
			const float boundRadius = Util::Adapter::GetNiBoundRadius(m_WorldBound);

			const float3 toCenter = center - ld.Position;
			const float dist = toCenter.Length();

			// Sphere vs light radius
			if (dist - boundRadius > ld.Radius)
				continue;

			// Sphere vs spot cone (conservative): the bound must reach the cone frustum.
			// CosOuterAngle is the half-angle cosine, matching the shader's smoothstep.
			if (ld.Type == LightType::Spot) {
				const float distAlong = toCenter.Dot(ld.Direction);

				// Entirely behind the light's apex plane
				if (distAlong < -boundRadius)
					continue;

				// cosOuter <= 0 means the cone is a hemisphere or wider — nothing to cull
				const float cosOuter = std::clamp(float(ld.CosOuterAngle), -1.0f, 1.0f);
				if (cosOuter > 0.0f) {
					const float perpDist = (toCenter - ld.Direction * distAlong).Length();
					const float coneRadius = std::max(distAlong, 0.0f) * std::tan(std::acos(cosOuter));
					if (perpDist - boundRadius > coneRadius)
						continue;
				}
			}

			lightIds[numLights] = light.m_Index;
			numLights++;
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
	auto sceneGraph = scene->GetSceneGraph();

	const bool skipInstanceLights = scene->m_Settings.ExperimentalSettings.GlobalLights;

	// Only those who affect geometry count or its flags
	if (m_DirtyFlags.any(DirtyFlags::Visibility, DirtyFlags::Mesh, DirtyFlags::Alpha)) {
		m_Flags.reset(Flags::Updatable, Flags::TwoSided);

		m_GeometryDescs.clear();
		m_GeometrySlots.clear();

		auto* meshManager = sceneGraph->GetMeshManager().get();

		for (const auto& mesh : m_Members) {
			if (mesh->IsHidden())
				continue;

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
			const auto materialIndex = static_cast<uint32_t>(mesh->GetMaterial()->GetOffset());

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
					0,
					static_cast<uint32_t>(geomTris.indexOffset),
					static_cast<uint32_t>(geomTris.vertexOffset),
					materialIndex
				);

				meshManager->WriteMeshData(entry.geometryIndex, md);
			}
		}
	}

	const uint32_t meshCount = static_cast<uint32_t>(m_GeometrySlots.size());

	m_IsValid = meshCount > 0;
	if (m_IsValid) {
		auto* camera = sceneGraph->GetCamera();

		const bool bypassFrustumCulling = m_Flags.all(Flags::Player) && sceneGraph->GetDrawFirstPerson();

		const bool inFrustum = bypassFrustumCulling || camera->PointInFrustum(m_WorldBound.center, Util::Adapter::GetNiBoundRadius(m_WorldBound));
		m_Flags.set(!inFrustum, Flags::FrustumCulled);

		if (!skipInstanceLights) {
			// TODO: Move this to the GPU - It doesn't scale well on CPU
			UpdateInstanceLightData(sceneGraph->GetLights(), sceneGraph->GetLightData());
		}
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
	const bool isSingleMemberOrphan = (m_Owner == nullptr && m_Members.size() <= 1);

	if (firstBuild || !m_BLAS || hasMesh || (!isSingleMemberOrphan && hasVisibility))
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
		.setInstanceMask(m_Flags.all(Flags::FrustumCulled) ? InstanceMask::FrustumCulled : InstanceMask::Default)
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
	if (buildMode == BuildMode::Rebuild)
		sceneGraph->RecordRebuild();
	else if (buildMode == BuildMode::Update)
		sceneGraph->RecordUpdate();
	else
		sceneGraph->RecordSkip();

	if (buildMode == BuildMode::Skip && m_Owner == nullptr && m_Members.size() <= 1 && m_DirtyFlags == DirtyFlags::Visibility) {
		// Single-mesh orphan clusters are excluded from the TLAS while hidden.
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
