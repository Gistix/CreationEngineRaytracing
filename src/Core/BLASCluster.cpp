#include "Core/BLASCluster.h"
#include "SceneGraph.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

#include <eastl/algorithm.h>

BLASCluster::BLASCluster(RE::TESObjectREFR* owner) :
	m_Owner(owner)
{
	if (m_Owner)
		m_Name = { std::format("Cluster {:08X}", m_Owner->GetFormID()).c_str() };
	else
		m_Name = { "Cluster (orphan)" };
}

void BLASCluster::MarkDirty()
{
	m_IsDirty = true;
}
	
void BLASCluster::AddMember(const eastl::shared_ptr<BaseMesh>& mesh)
{
	m_Members.push_back(mesh);
	mesh->SetCluster(this);
	
	if (mesh->IsUpdatable())
		m_Updatable = true;
	
	m_MembershipDirty = true;
}


void BLASCluster::RemoveMember(BaseMesh* mesh)
{
	mesh->SetCluster(nullptr);

	const size_t before = m_Members.size();

	m_Members.erase(
		eastl::remove_if(m_Members.begin(), m_Members.end(),
			[mesh](const eastl::weak_ptr<BaseMesh>& member) { return member.lock().get() == mesh; }),
		m_Members.end());
	
	if (m_Members.size() != before) {
		m_MembershipDirty = true;
	}
}


void BLASCluster::GrowBounds(const RE::NiBound& bound)
{
	float3 boundCenter = Util::Math::Float3(bound.center);

	float margin = m_InstanceRadius - bound.radius;
	if (margin > 0.0f) {
		float distSq = (boundCenter - m_ClusterCenter).LengthSquared();
		if (distSq <= margin * margin)
			return;
	}

	float distToBound = (boundCenter - m_ClusterCenter).Length();
	m_InstanceRadius = std::max(m_InstanceRadius, distToBound + bound.radius);
}

bool BLASCluster::Empty() const
{
	for (const auto& member : m_Members)
		if (!member.expired())
			return false;

	return true;
}

static InstanceLightData ComputeInstanceLightData(
	const float3x4& instanceTransform,
    float instanceRadius,
    const eastl::map<RE::BSLight*, Light>& lights,
    const eastl::array<LightData, Constants::LIGHTS_MAX>& lightData)
{
	uint8_t lightIds[Constants::INSTANCE_LIGHTS_MAX];
	uint8_t numLights = 0;

	float3 clusterPosition = float3(instanceTransform._14, instanceTransform._24, instanceTransform._34);

	for (const auto& [bsLight, light] : lights) {
		if (!light.m_Active)
			continue;

		if (numLights >= Constants::INSTANCE_LIGHTS_MAX) {
			logger::error("ComputeInstanceLightData - Number of lights per instance of {} exceeds the maximum of {}, for light {} of {}", 
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
			float dist = float3::Distance(clusterPosition, ld.Vector);
			if (dist - instanceRadius <= ld.Radius) {
				lightIds[numLights] = light.m_Index;
				numLights++;
			}
		}
	}

	return InstanceLightData(lightIds, numLights);
}

bool BLASCluster::GetData(MeshData* meshData, uint32_t& meshCount, InstanceData& outInstance,
                          const eastl::map<RE::BSLight*, Light>& lights,
                          const eastl::array<LightData, Constants::LIGHTS_MAX>& lightData) const
{
	// Mirror BuildUpdate's visible-member-geometry iteration so MeshData order matches the BLAS geometry order.
	const uint32_t firstGeometry = meshCount;
	uint32_t numGeometry = 0;

	for (const auto& member : m_Members) {
		auto mesh = member.lock();
		if (!mesh || mesh->IsHidden())
			continue;

		const auto& descs = mesh->GetGeometryDescs();
		if (descs.empty())
			continue;

		if (meshCount + descs.size() > Constants::NUM_MESHES_MAX)
			break;

		const uint32_t written = mesh->WriteMeshData(&meshData[meshCount]);
		meshCount += written;
		numGeometry += written;
	}

	if (numGeometry == 0)
		return false;

	outInstance = {};
	outInstance.Transform = m_InstanceTransform;
	outInstance.PrevTransform = m_HasPrevInstanceTransform ? m_PrevInstanceTransform : m_InstanceTransform;

	// Advance previous-frame state (GetData runs once per cluster per frame).
	m_PrevInstanceTransform = m_InstanceTransform;
	m_HasPrevInstanceTransform = true;
	outInstance.FirstGeometryID = firstGeometry;
	outInstance.NumGeometry = numGeometry;
	outInstance.Alpha = 1.0f;

	outInstance.LightData = ComputeInstanceLightData(m_InstanceTransform, m_InstanceRadius, lights, lightData);
	m_InstanceRadius = 0.0f;

	return true;
}

nvrhi::rt::AccelStructDesc BLASCluster::MakeDesc(bool update) const
{
	auto blasDesc = nvrhi::rt::AccelStructDesc()
		.setIsTopLevel(false)
		.setDebugName(m_Name.c_str());

	// Updatable clusters favour fast builds (frequent refits); static clusters favour fast traversal.
	blasDesc.buildFlags = m_Updatable
		? nvrhi::rt::AccelStructBuildFlags::PreferFastBuild
		: nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;

	blasDesc.buildFlags |= (update
		? nvrhi::rt::AccelStructBuildFlags::PerformUpdate
		: nvrhi::rt::AccelStructBuildFlags::AllowUpdate);

	return blasDesc;
}

void BLASCluster::BuildUpdate(nvrhi::ICommandList* commandList, SceneGraph* sceneGraph)
{
	m_IsDirty = false;
	auto* renderer = Renderer::GetSingleton();
	auto* device = renderer->GetDevice();
	const auto frameIndex = renderer->GetFrameIndex();

	if (frameIndex == m_LastBuild) {
		logger::info("BLASCluster::BuildUpdate - {} already built this frame, skipping", m_Name);
		return;
	}

	// Pull dirty state from the members; upload any pending GPU data while we're here.
	bool anyStructure = false;
	bool anyUpdate = false;

	m_Updatable = false;

	size_t liveMemberCount = 0;
	for (const auto& member : m_Members) {
		auto mesh = member.lock();
		if (!mesh)
			continue;
		liveMemberCount++;

		if (mesh->IsUpdatable())
			m_Updatable = true;

		const auto dirty = mesh->GetDirtyFlags();

		if (dirty.any(DirtyFlags::Visibility))
			anyStructure = true;

		if (dirty.any(DirtyFlags::Transform, DirtyFlags::Vertex)) {
			anyUpdate = true;

			if (dirty.any(DirtyFlags::Vertex))
				mesh->UploadBuffers(commandList);
		}
	}

	// Decide what to do from dirtiness only. An empty/failed cluster keeps m_BLAS == null, so basing
	// this on (!m_BLAS) would force a "rebuild" every frame forever; gate the first build on m_LastBuild instead.
	const bool firstBuild = (m_LastBuild == Constants::INVALID_FRAME_INDEX);

	bool rebuild;

	if (firstBuild || m_MembershipDirty || anyStructure) {
		rebuild = true;
		m_NumUpdatesSinceRebuild = 0;
	}
	else if (anyUpdate) {
		if (m_NumUpdatesSinceRebuild >= Constants::MAX_BLAS_UPDATES_BEFORE_MAINTENANCE) {
			if (sceneGraph->TryMaintenanceRebuild(frameIndex)) {
				rebuild = true;
				m_NumUpdatesSinceRebuild = 0;
			}
			else {
				rebuild = false;
			}
		}
		else {
			m_NumUpdatesSinceRebuild++;
			rebuild = false;
		}
	}
	else {
		logger::warn("BLASCluster::BuildUpdate - {} no work needed (live members: {}, firstBuild: {}, membershipDirty: {}, anyStructure: {}, anyUpdate: {})",
			m_Name, liveMemberCount, firstBuild, m_MembershipDirty, anyStructure, anyUpdate);
		return;
	}

	// Aggregate geometry from live, visible members. Transforms are already baked into the descs
	// (SetLocalToOwner during traversal), so no owner/trishape dereference happens here.
	m_GeometryDescs.clear();

	for (const auto& member : m_Members) {
		auto mesh = member.lock();
		if (!mesh || mesh->IsHidden())
			continue;

		for (const auto& desc : mesh->GetGeometryDescs())
			m_GeometryDescs.push_back(desc);
	}

	// Consume member dirty state now that it has been folded into this build.
	for (const auto& member : m_Members)
		if (auto mesh = member.lock())
			mesh->ClearDirtyFlags();

	if (m_GeometryDescs.empty()) {
		m_BLAS = nullptr;
		m_MembershipDirty = false;
		m_LastBuild = frameIndex;
		return;
	}

	// Allocate a new accel struct on first build or when the required size grows; a refit needs an
	// existing BLAS, so if there isn't one yet, promote to a full rebuild.
	bool allocate = !m_BLAS;
	if (allocate)
		rebuild = true;

	auto blasDesc = MakeDesc(!rebuild);

	for (const auto& desc : m_GeometryDescs)
		blasDesc.addBottomLevelGeometry(desc);

	if (!allocate && rebuild) {
		auto prebuildInfo = device->getAccelStructPreBuildInfo(blasDesc);
		allocate = prebuildInfo.resultMaxSizeInBytes > m_BLAS->getBufferSize();
	}

	if (allocate)
		m_BLAS = device->createAccelStruct(blasDesc);

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, m_BLAS, blasDesc);

	m_MembershipDirty = false;
	m_LastBuild = frameIndex;
}
