#include "Core/BLASCluster.h"
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
		XMStoreFloat3x4(&m_Transform, Util::Math::GetXMFromNiTransform(object->world));
		XMStoreFloat3x4(&m_PrevTransform, Util::Math::GetXMFromNiTransform(object->previousWorld));
	}
	else {
		if (m_Members.empty()) {
			m_Transform = Constants::kIdentityTransform;
			m_PrevTransform = Constants::kIdentityTransform;
		}
		else {
			const auto& mesh = m_Members.front();
			m_Transform = mesh->GetTransform();
			m_PrevTransform = mesh->GetPrevTransform();
		}
	}

	m_ClusterPosition = float3(m_Transform._14, m_Transform._24, m_Transform._34);
}

bool BLASCluster::Empty() const
{
	return m_Members.empty();
}

bool BLASCluster::Valid() const
{
	for (const auto& mesh : m_Members) {
		if (mesh->IsHidden())
			continue;

		const auto& descs = mesh->GetGeometryDescs();
		if (descs.empty())
			continue;

		return true;
	}

	return false;
}

InstanceLightData BLASCluster::GetInstanceLightData(
    const eastl::map<RE::BSLight*, Light>& lights,
    const eastl::array<LightData, Constants::LIGHTS_MAX>& lightData)
{
	uint8_t lightIds[Constants::INSTANCE_LIGHTS_MAX];
	uint8_t numLights = 0;

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
			float dist = float3::Distance(m_ClusterPosition, ld.Vector);
			if (dist - m_ClusterRadius <= ld.Radius) {
				lightIds[numLights] = light.m_Index;
				numLights++;
			}
		}
	}

	return InstanceLightData(lightIds, numLights);
}

uint32_t BLASCluster::GetMeshEntryCount() const
{
	uint32_t count = 0;
	for (const auto& mesh : m_Members) {
		if (mesh->IsHidden())
			continue;
		count += static_cast<uint32_t>(mesh->GetGeometryDescs().size());
	}
	return count;
}

void BLASCluster::Update(MeshData* meshData, InstanceData* instanceData,
	uint32_t meshStart, uint32_t instanceIndex,
    const eastl::map<RE::BSLight*, Light>& lights,
     const eastl::array<LightData, Constants::LIGHTS_MAX>& lightData)
{
	m_ClusterRadius = 0.0f;

	UpdateTransform();

	const auto invTransform = XMMatrixInverse(nullptr, DirectX::XMLoadFloat3x4(&m_Transform));
	const auto invPrevTransform = XMMatrixInverse(nullptr, DirectX::XMLoadFloat3x4(&m_PrevTransform));

	const auto isOrigin = float3(m_Transform._14, m_Transform._24, m_Transform._34) == float3::Zero;

	uint32_t meshCount = meshStart;

	m_GeometryDescs.clear();
	m_GeometryDescs.reserve(m_Members.size());

	m_Flags.reset(Flags::Updatable, Flags::TwoSided);

	for (const auto& mesh : m_Members) {
		if (mesh->IsHidden())
			continue;

		GrowBounds(mesh->GetWorldBound());

		mesh->UpdateLocalTransform(invTransform, invPrevTransform, isOrigin);

		const auto& descs = mesh->GetGeometryDescs();
		if (descs.empty())
			continue;

		for (const auto& desc : descs)
			m_GeometryDescs.push_back(desc);

		if (mesh->IsUpdatable())
			m_Flags.set(Flags::Updatable);

		if (mesh->IsTwoSided())
			m_Flags.set(Flags::TwoSided);

		m_DirtyFlags |= mesh->GetDirtyFlags();

		meshCount += mesh->WriteMeshData(&meshData[meshCount]);
	}

	const uint32_t numGeometry = meshCount - meshStart;

	if (numGeometry == 0) {
		logger::warn("BLASCluster::GetData - BLASCluster {} has no geometry", fmt::ptr(this));
		return;
	}

	InstanceData& outInstance = instanceData[instanceIndex];
	outInstance.Transform = m_Transform;
	outInstance.PrevTransform = m_PrevTransform;

	outInstance.LightData = GetInstanceLightData(lights, lightData);

	outInstance.FirstGeometryID = meshStart;
	outInstance.NumGeometry = numGeometry;
	outInstance.Alpha = 1.0f;

	return;
}

nvrhi::rt::AccelStructDesc BLASCluster::MakeDesc(bool update) const
{
	auto blasDesc = nvrhi::rt::AccelStructDesc()
		.setIsTopLevel(false)
		.setDebugName(m_Name.c_str());

	// Updatable clusters favour fast builds (frequent refits); static clusters favour fast traversal.
	blasDesc.buildFlags = m_Flags.all(Flags::Updatable)
		? nvrhi::rt::AccelStructBuildFlags::PreferFastBuild
		: nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;

	blasDesc.buildFlags |= (update
		? nvrhi::rt::AccelStructBuildFlags::PerformUpdate
		: nvrhi::rt::AccelStructBuildFlags::AllowUpdate);

	return blasDesc;
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

	if (frameIndex == m_LastBuild) {
		logger::info("BLASCluster::BuildUpdate - {} already built this frame, skipping", m_Name);
		return;
	}

	// Pull dirty state from the members; upload any pending GPU data while we're here.
	const bool anyStructure = m_DirtyFlags.any(DirtyFlags::Visibility, DirtyFlags::Mesh);
	const bool anyUpdate = m_DirtyFlags.any(DirtyFlags::Vertex, DirtyFlags::Skin, DirtyFlags::Transform);

	// Decide what to do from dirtiness only. An empty/failed cluster keeps m_BLAS == null, so basing
	// this on (!m_BLAS) would force a "rebuild" every frame forever; gate the first build on m_LastBuild instead.
	const bool firstBuild = (m_LastBuild == Constants::INVALID_FRAME_INDEX);

	bool rebuild = false;

	if (firstBuild || anyStructure) {
		rebuild = true;
	}
	else if (anyUpdate) {
		if (m_NumUpdatesSinceRebuild >= Constants::MAX_BLAS_UPDATES_BEFORE_MAINTENANCE) {
			rebuild = sceneGraph->TryMaintenanceRebuild(frameIndex);
		}
	}
	else {
		logger::warn("BLASCluster::BuildUpdate - {} no work needed (firstBuild: {}, anyStructure: {}, anyUpdate: {})",
			m_Name, firstBuild, anyStructure, anyUpdate);
		return;
	}

	if (rebuild)
		m_NumUpdatesSinceRebuild = 0;
	else
		m_NumUpdatesSinceRebuild++;

	if (m_GeometryDescs.empty()) {
		m_BLAS = nullptr;
		m_LastBuild = frameIndex;
		m_DirtyFlags.reset();
		return;
	}

	// Allocate a new accel struct on first build or when the required size grows; a refit needs an
	// existing BLAS, so if there isn't one yet, promote to a full rebuild.
	bool allocate = !m_BLAS;
	if (allocate)
		rebuild = true;

	auto blasDesc = MakeDesc(!rebuild);
	blasDesc.bottomLevelGeometries = m_GeometryDescs;

	if (!allocate && rebuild) {
		auto prebuildInfo = device->getAccelStructPreBuildInfo(blasDesc);
		allocate = prebuildInfo.resultMaxSizeInBytes > m_BLAS->getBufferSize();
	}

	if (allocate)
		m_BLAS = device->createAccelStruct(blasDesc);

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, m_BLAS, blasDesc);

	m_DirtyFlags.reset();
	m_LastBuild = frameIndex;
}
