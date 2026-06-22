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

void BLASCluster::AddMember(const eastl::shared_ptr<BaseMesh>& mesh)
{
	m_Members.push_back(mesh);

	if (mesh->IsUpdatable())
		m_Updatable = true;

	m_MembershipDirty = true;
}

void BLASCluster::RemoveMember(BaseMesh* mesh)
{
	const size_t before = m_Members.size();

	m_Members.erase(
		eastl::remove_if(m_Members.begin(), m_Members.end(),
			[mesh](const eastl::weak_ptr<BaseMesh>& member) { return member.lock().get() == mesh; }),
		m_Members.end());

	if (m_Members.size() != before)
		m_MembershipDirty = true;
}

bool BLASCluster::Empty() const
{
	for (const auto& member : m_Members)
		if (!member.expired())
			return false;

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
	auto* renderer = Renderer::GetSingleton();
	auto* device = renderer->GetDevice();
	const auto frameIndex = renderer->GetFrameIndex();

	if (frameIndex == m_LastBuild)
		return;

	// Pull dirty state from the members; upload any pending GPU data while we're here.
	bool anyStructure = false;
	bool anyUpdate = false;

	m_Updatable = false;

	for (const auto& member : m_Members) {
		auto mesh = member.lock();
		if (!mesh)
			continue;

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
		return;
	}

	logger::trace("BLASCluster {} - {} - Rebuild: {}, AnyUpdate: {}, AnyStructure: {}, MembershipDirty: {}", fmt::ptr(this), m_Name, rebuild, anyUpdate, anyStructure, m_MembershipDirty);

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
