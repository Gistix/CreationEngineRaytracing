#include "Core/BLASCluster.h"
#include "Core/BLASManager.h"
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
	{
		std::scoped_lock lock(m_MemberMutex);
		auto [it, inserted] = m_MemberSet.emplace(mesh);
		if (!inserted)
			return;

		m_Members.push_back(mesh);
	}

	mesh->SetCluster(this);

	{
		std::scoped_lock lock(m_DirtyMutex);		
		m_DirtyFlags.set(DirtyFlags::Mesh);
	}
}

void BLASCluster::RemoveMember(BaseMesh* mesh)
{
	{
		std::scoped_lock lock(m_MemberMutex);
		const bool removed = m_MemberSet.erase(mesh);
		if (!removed)
			return;

		m_Members.erase_last(mesh);
	}

	mesh->SetCluster(nullptr);

	{
		std::scoped_lock lock(m_DirtyMutex);
		m_DirtyFlags.set(DirtyFlags::Mesh);
	}
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

			m_WorldBound = mesh->GetWorldBound();
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

nvrhi::rt::IAccelStruct* BLASCluster::GetBLAS() const
{
	if (m_BLASResource && m_BLASResource->m_BLAS)
		return m_BLASResource->m_BLAS.Get();
	return m_BLAS.Get();
}

void BLASCluster::UpdateDirtyFlags(const DirtyFlags& meshDirtyFlags)
{
	std::scoped_lock lock(m_DirtyMutex);
	m_DirtyFlags.set(meshDirtyFlags);
}

uint32_t BLASCluster::Update()
{
	UpdateTransform();

	auto sceneGraph = Scene::GetSingleton()->GetSceneGraph();

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

		auto* blasManager = sceneGraph->GetBLASManager().get();
		if (m_GeometryDescs.empty()) {
			m_BLASResource = nullptr;
			m_BLAS = nullptr;
		}
		else if (m_Flags.all(Flags::Updatable)) {
			std::vector<nvrhi::BufferHandle> bufferRefs;
			for (const auto& d : m_GeometryDescs) {
				if (d.geometryType == nvrhi::rt::GeometryType::Triangles) {
					if (d.geometryData.triangles.vertexBuffer)
						bufferRefs.push_back(d.geometryData.triangles.vertexBuffer);

					if (d.geometryData.triangles.indexBuffer)
						bufferRefs.push_back(d.geometryData.triangles.indexBuffer);
				}
			}
			auto buildFlags = MakeDesc(BuildMode::Rebuild).buildFlags;
			m_BLASResource = blasManager->CreateDedicatedBLAS(m_GeometryDescs, buildFlags, bufferRefs, m_Name);
			m_BLAS = m_BLASResource->m_BLAS;
		}
		else {
			BLASKey key;
			key.baseFormID = (m_Owner && m_Owner->GetObjectReference()) ? m_Owner->GetObjectReference()->GetFormID() : 0;
			key.buildFlags = static_cast<uint32_t>(MakeDesc(BuildMode::Rebuild).buildFlags);

			std::vector<nvrhi::BufferHandle> bufferRefs;
			std::vector<nvrhi::rt::GeometryDesc> sharedDescs;
			sharedDescs.reserve(m_GeometryDescs.size());

			RE::NiTransform rootWorldInv;
			bool hasRootWorldInv = false;
			if (m_Owner) {
				auto* object = m_Owner->Get3D(false);
				if (object) {
					rootWorldInv = object->world.Invert();
					hasRootWorldInv = true;
				}
			}

			for (const auto& mesh : m_Members) {
				if (mesh->IsHidden())
					continue;

				float3x4 relTransform = Constants::kIdentityTransform;
				bool hasRelTransform = false;

				if (hasRootWorldInv) {
					relTransform = Util::Math::ComputeLocalToRoot(rootWorldInv, mesh->GetWorld());
					hasRelTransform = !Util::Math::MatrixNearEqual(relTransform, Constants::kIdentityTransform);
				}

				const auto& entries = mesh->GetGeometryEntries();
				for (const auto& entry : entries) {
					auto geomDesc = entry.desc;
					const auto& geomTris = geomDesc.geometryData.triangles;

					if (geomTris.vertexBuffer)
						bufferRefs.push_back(geomTris.vertexBuffer);
					if (geomTris.indexBuffer)
						bufferRefs.push_back(geomTris.indexBuffer);

					if (hasRelTransform) {
						const uint64_t transformOffset = blasManager->StageRelativeTransform(relTransform);
						geomDesc.setTransformBuffer(blasManager->GetSharedTransformBuffer(), transformOffset);
						geomDesc.useTransform = true;
					} else {
						geomDesc.transformBuffer = nullptr;
						geomDesc.transformBufferOffset = 0;
						geomDesc.useTransform = false;
					}

					sharedDescs.push_back(geomDesc);

					BLASGeometryKey geomKey;
					geomKey.nativeIndexBuffer = GetNativeBufferPointer(geomTris.indexBuffer);
					geomKey.indexOffset = geomTris.indexOffset;
					geomKey.indexCount = geomTris.indexCount;
					geomKey.nativeVertexBuffer = GetNativeBufferPointer(geomTris.vertexBuffer);
					geomKey.vertexOffset = geomTris.vertexOffset;
					geomKey.vertexCount = geomTris.vertexCount;
					geomKey.vertexStride = geomTris.vertexStride;
					geomKey.geometryFlags = static_cast<uint32_t>(geomDesc.flags);
					geomKey.hasRelativeTransform = hasRelTransform;
					geomKey.relativeTransform = hasRelTransform ? relTransform : Constants::kIdentityTransform;
					geomKey.ommHash = mesh->GetOmmHash();

					key.geometries.push_back(geomKey);
				}
			}

			key.ComputeHash();

			auto buildFlags = MakeDesc(BuildMode::Rebuild).buildFlags;
			m_BLASResource = blasManager->GetOrCreateSharedBLAS(key, sharedDescs, buildFlags, bufferRefs, m_Name);
			m_BLAS = m_BLASResource->m_BLAS;
			m_GeometryDescs = sharedDescs;
		}
	}

	const uint32_t meshCount = static_cast<uint32_t>(m_GeometrySlots.size());

	m_IsValid = meshCount > 0;
	if (m_IsValid) {
		auto* camera = sceneGraph->GetCamera();

		const bool bypassFrustumCulling = m_Flags.all(Flags::Player) && sceneGraph->GetDrawFirstPerson();

		const bool inFrustum = bypassFrustumCulling || camera->PointInFrustum(m_WorldBound.center, Util::Adapter::GetNiBoundRadius(m_WorldBound));
		m_Flags.set(!inFrustum, Flags::FrustumCulled);
	}

	return meshCount;
}

void BLASCluster::AppendInstanceDescs(eastl::vector<nvrhi::rt::InstanceDesc>& outDescs) const
{
	outDescs.push_back(MakeInstanceDesc());
}

void BLASCluster::WriteInstanceData(uint32_t firstMesh, uint32_t meshCount, InstanceData* outInstances, float4* outBounds) const
{
	InstanceData& instanceData = outInstances[0];
	instanceData.Transform = m_Transform;
	instanceData.PrevTransform = m_PrevTransform;
	instanceData.LightData = {};
	instanceData.FirstGeometryID = firstMesh;
	instanceData.NumGeometry = meshCount;
	instanceData.Alpha = 1.0f;

	const float3 center = Util::Math::Float3(m_WorldBound.center);
	outBounds[0] = float4(center.x, center.y, center.z, Util::Adapter::GetNiBoundRadius(m_WorldBound));
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
	const bool hasAlpha = m_DirtyFlags.any(DirtyFlags::Alpha);
	const bool hasUpdate = m_DirtyFlags.any(DirtyFlags::Vertex, DirtyFlags::Skin, DirtyFlags::Transform);
	const bool isOrphan = (m_Owner == nullptr);

	const bool isSharedAndBuilt = m_BLASResource && m_BLASResource->m_IsShared && m_BLASResource->IsValid() && !m_BLASResource->m_IsDirty;

	if (!isSharedAndBuilt) {
		if (firstBuild || !GetBLAS() || hasMesh || hasAlpha || (!isOrphan && hasVisibility))
			return BuildMode::Rebuild;
	} else if (!GetBLAS() || hasAlpha) {
		return BuildMode::Rebuild;
	}

	if (hasUpdate) {
		if (m_Flags.all(Flags::Updatable)) {
			if (m_UpdateCount >= Constants::MAX_BLAS_UPDATES_BEFORE_MAINTENANCE &&
				sceneGraph->TryMaintenanceRebuild(frameIndex))
				return BuildMode::Rebuild;

			return BuildMode::Update;
		}

		return BuildMode::Skip;
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
		.setBLAS(GetBLAS());

	return instanceDesc;
}

void BLASCluster::BuildUpdate(nvrhi::ICommandList* commandList, SceneGraph* sceneGraph)
{
	auto* renderer = Renderer::GetSingleton();
	const auto frameIndex = renderer->GetFrameIndex();

	if (frameIndex == m_LastBuildFrame) {
		return;
	}

	const auto buildMode = DetermineBuildMode(sceneGraph, frameIndex);
	if (buildMode == BuildMode::Skip) {
		if (m_BLASResource && m_BLASResource->IsValid()) {
			m_BLAS = m_BLASResource->m_BLAS;
		}
		m_DirtyFlags.reset();
		m_LastBuildFrame = frameIndex;
		return;
	}

	if (buildMode == BuildMode::Rebuild)
		m_UpdateCount = 0;
	else
		m_UpdateCount++;

	if (m_GeometryDescs.empty() || !m_BLASResource) {
		m_BLAS = nullptr;
		m_LastBuildFrame = frameIndex;
		m_DirtyFlags.reset();
		return;
	}

	auto* blasManager = sceneGraph->GetBLASManager().get();
	blasManager->BuildResource(commandList, m_BLASResource, buildMode == BuildMode::Rebuild);
	m_BLAS = m_BLASResource->m_BLAS;

	m_DirtyFlags.reset();
	m_LastBuildFrame = frameIndex;
}
