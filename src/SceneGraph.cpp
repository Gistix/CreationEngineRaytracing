#include "SceneGraph.h"

#include "Scene.h"

#include "core/Mesh.h"

#include "Renderer.h"
#include "Util.h"
#include "ShaderUtils.h"

#include "Types/RE/RE.h"
#if defined(SKYRIM)
#include "Types/CommunityShaders/LightLimitFix.h"
#include "Types/CommunityShaders/ISLCommon.h"
#endif

#include "Pass/Raytracing/Common/Skinning.h"

#include "Core/SkinnedMesh.h"
#include "Core/DynamicMesh.h"

void SceneGraph::Initialize()
{
	auto device = Renderer::GetSingleton()->GetDevice();

	// Mesh Data Buffer
	m_MeshBuffer = Util::CreateStructuredBuffer<MeshData>(device, Constants::NUM_MESHES_MAX, "Mesh Buffer");

	// Instance Data Buffer
	m_InstanceBuffer = Util::CreateStructuredBuffer<InstanceData>(device, Constants::NUM_INSTANCES_MAX, "Instance Buffer");

	// Mesh Data Buffer
	m_LightBuffer = Util::CreateStructuredBuffer<LightData>(device, Constants::LIGHTS_MAX, "Light Buffer");

	// Triangle bindless descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1).setSize(UINT_MAX)
		};

		m_TriangleDescriptors = eastl::make_unique<BindlessTableManager>(device, bindlessLayoutDesc, true);
	}

	// Vertex bindless descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::RawBuffer_SRV(2).setSize(UINT_MAX)
		};

		m_VertexDescriptors = eastl::make_unique<BindlessTableManager>(device, bindlessLayoutDesc, true);
	}

	// Material bindless descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3).setSize(UINT_MAX)
		};

		m_MaterialDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	// Dynamic Vertex bindless descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1).setSize(UINT_MAX)
		};

		m_DynamicVertexDescriptors = eastl::make_unique<BindlessTableManager>(device, bindlessLayoutDesc, true);
	}

	// Skinning descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3).setSize(UINT_MAX)
		};

		m_SkinningDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	// Vertex copy descriptor table (original/rest-pose vertices in native packed format; raw views)
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::RawBuffer_SRV(2).setSize(UINT_MAX)
		};

		m_VertexCopyDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	// Vertex write descriptor table (live vertices in native packed format; raw UAV)
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::RawBuffer_UAV(0).setSize(UINT_MAX)
		};

		m_VertexWriteDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	// Dynamic vertex write descriptor table (skinned dynamic float4 positions; UAV)
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2).setSize(UINT_MAX)
		};

		m_DynamicVertexWriteDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	// Previous position SRV descriptor table (for reading prev positions in RT shaders)
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6).setSize(UINT_MAX)
		};

		m_PrevPositionDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	// Previous position UAV descriptor table (for writing prev positions in skinning shader)
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1).setSize(UINT_MAX)
		};

		m_PrevPositionWriteDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	m_TextureManager = eastl::make_unique<TextureManager>();
}

void SceneGraph::UpdateCamera()
{
	const auto* tesCamera = RE::PlayerCamera::GetSingleton()->currentState->camera;
	m_Camera = tesCamera ? Util::Game::FindNiCamera(tesCamera->cameraRoot.get()) : nullptr;
}

void SceneGraph::UpdateLights([[maybe_unused]] nvrhi::ICommandList* commandList)
{
#if defined(SKYRIM)
	auto& mainSSNRuntimeData = Util::Adapter::GetShaderManagerState().shadowSceneNode[0]->GetRuntimeData();

	// Update Light Vector
	{
		m_TempActiveLights.clear();
		m_TempActiveLights.reserve(mainSSNRuntimeData.activeLights.size() + mainSSNRuntimeData.activeShadowLights.size());

		auto collectLights = [&](const auto& lights) {
			for (const auto& activeLight : lights)
			{
				auto* ptr = activeLight.get();
				m_TempActiveLights.insert(ptr);
				m_Lights.try_emplace(ptr, ptr);
			}
		};

		collectLights(mainSSNRuntimeData.activeLights);
		collectLights(mainSSNRuntimeData.activeShadowLights);

		for (auto it = m_Lights.begin(); it != m_Lights.end(); )
		{
			if (!m_TempActiveLights.contains(it->first))
				it = m_Lights.erase(it);
			else
				++it;
		}
	}

	const auto& lightingSettings = Scene::GetSingleton()->m_Settings.LightingSettings;

	uint numLights = 0;

	for (auto& [bsLight, light] : m_Lights)
	{
		light.m_Active = true;
		light.m_Index = static_cast<uint8_t>(numLights);

		auto niLight = bsLight->light.get();

#if defined(SKYRIM)
		if (niLight->GetFlags().any(RE::NiAVObject::Flag::kHidden))
#elif defined(FALLOUT4)
		if (niLight->GetFlags() & static_cast<uint64_t>(CESEAdapter::RE::NiAVObjectFlag::kHidden))
#endif
			light.m_Active = false;

#if defined(SKYRIM)
		if (bsLight->IsShadowLight())
		{
			auto* shadowLight = reinterpret_cast<RE::BSShadowLight*>(bsLight);

			if (shadowLight->GetRuntimeData().maskIndex == 255)
				light.m_Active = false;
		}
#endif

		auto runtimeData = Util::Adapter::GetLightRuntimeData(niLight);

#if defined(SKYRIM)
		auto flags = std::bit_cast<LightLimitFix::LightFlags>(runtimeData.ambient.red);

		if (flags & LightLimitFix::LightFlags::Disabled)
			light.m_Active = false;
#endif

		// Update Light Data
		{
			auto& lightData = m_LightData[numLights];

			lightData.Color = float3(runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue);

			lightData.Radius = runtimeData.radius.x;

			if ((lightData.Color.x + lightData.Color.y + lightData.Color.z) <= 1e-4 || lightData.Radius <= 1e-4)
				light.m_Active = false;

			// Clear instances
			light.m_Instances.clear();

			if (light.m_Active)
				light.UpdateInstances();

			lightData.Vector = Util::Math::Float3(niLight->world.translate);

			lightData.InvRadius = 1.0f / runtimeData.radius.x;

			lightData.Fade = runtimeData.fade;

			if (lightingSettings.LodDimmer)
				lightData.Fade *= bsLight->lodDimmer;

			// Determine light type: Spot, Point, or Directional
			// NiSpotLight extends NiPointLight; both have BSLight::pointLight = true.
			// We distinguish spots via NiRTTI name check.
			bool isSpot = false;
			if (bsLight->pointLight) {
				auto* rtti = niLight->GetRTTI();
				if (rtti && rtti->name && std::strcmp(rtti->name, "NiSpotLight") == 0)
					isSpot = true;
			}

			if (isSpot) {
				lightData.Type = LightType::Spot;

				// Spot direction from world rotation matrix, first column = model direction (1,0,0) transformed
				auto& rot = niLight->world.rotate;
				float3 dir(rot.entry[0][0], rot.entry[1][0], rot.entry[2][0]);
				dir = Util::Math::Normalize(dir);
				lightData.Direction = dir;

				auto pointLightData = Util::Adapter::GetPointLightRuntimeData(niLight);
				float outerAngleDeg = pointLightData.spotOuterAngle;
				float innerAngleDeg = pointLightData.spotInnerAngle;

				// Clamp to valid range
				outerAngleDeg = std::clamp(outerAngleDeg, 1.0f, 89.0f);
				innerAngleDeg = std::clamp(innerAngleDeg, 0.0f, outerAngleDeg);

				lightData.CosOuterAngleHalf = DirectX::PackedVector::XMConvertFloatToHalf(std::cos(outerAngleDeg * (3.14159265f / 180.0f)));
				lightData.CosInnerAngleHalf = DirectX::PackedVector::XMConvertFloatToHalf(std::cos(innerAngleDeg * (3.14159265f / 180.0f)));
			} else {
				lightData.Type = bsLight->pointLight ? LightType::Point : LightType::Directional;
				lightData.Direction = float3(0.0f, 0.0f, 0.0f);
				lightData.CosOuterAngleHalf = DirectX::PackedVector::XMConvertFloatToHalf(-1.0f);
				lightData.CosInnerAngleHalf = DirectX::PackedVector::XMConvertFloatToHalf(-1.0f);
			}

			lightData.Flags = 0;

#if defined(SKYRIM)
			if (flags & LightLimitFix::LightFlags::InverseSquare) {
				lightData.Flags |= LightFlags::ISL;

				auto* extData = ISLCommon::RuntimeLightDataExt::Get(niLight);

				lightData.Fade *= 4.0f;
				lightData.FadeZone = 1.f / (lightData.Radius * std::clamp(ISLCommon::FadeZoneBase * lightData.InvRadius, 0.f, 1.f));
				lightData.SizeBias = ISLCommon::ScaledUnitsSq * extData->size * extData->size * 0.5f;
			}

			if (flags & LightLimitFix::LightFlags::Linear)
				lightData.Flags |= LightFlags::LinearLight;
#endif
		}

		numLights++;

		if (numLights >= Constants::LIGHTS_MAX) {
			logger::error("SceneGraph::UpdateLights - Number of lights {} exceeds the maximum of {}", numLights, Constants::LIGHTS_MAX);
			break;
		}
	}

	commandList->writeBuffer(m_LightBuffer, m_LightData.data(), numLights * sizeof(LightData));
#endif
}

void SceneGraph::UpdateActors()
{
	for (auto& [formID, actorRef]: m_Actors)
	{
		actorRef.Update();
	}
}

void SceneGraph::UpdateLODVisibility()
{
	for (auto& [block, ref] : m_TerrainLODInstances)
	{
		ref->UpdateVisibility();
	}

	for (auto& [block, ref] : m_ObjectLODInstances)
	{
		ref->UpdateVisibility();
	}

#if defined(SKYRIM)
	for (auto& [block, ref] : m_TreeLODInstances)
	{
		ref->UpdateVisibility();
	}
#endif
}

void SceneGraph::OnDestroy(RE::BSTriShape* bsTriShape)
{
	BaseMesh* mesh = nullptr;
	{
		std::scoped_lock lock(m_MeshMutex);

		auto it = m_DirectMeshes.find(bsTriShape);
		if (it == m_DirectMeshes.end())
			return;

		mesh = it->second.get();
	}

	// Signal destroy outside mesh mutex since it has its own scoped lock inside
	mesh->OnDestroy();

	{
		std::scoped_lock lock(m_MeshDestroyMutex);
		m_DestroyedMeshes.push_back(bsTriShape);
	}
}

void SceneGraph::UpdateDynamicData(RE::BSDynamicTriShape* bsDynamicTriShape)
{
	BaseMesh* mesh = nullptr;
	{
		std::scoped_lock lock(m_MeshMutex);

		auto it = m_DirectMeshes.find(bsDynamicTriShape);
		if (it == m_DirectMeshes.end())
			return;

		mesh = it->second.get();
	}

	if (auto dynamicMesh = mesh->AsDynamicMesh()) {
		auto& runtimeData = bsDynamicTriShape->GetDynamicTrishapeRuntimeData();

		// Function is called through a hook thats already between lock
		// Acessing without locking here is safe and correct
		dynamicMesh->UpdateDynamicData(runtimeData.dynamicData, runtimeData.dataSize);
	}
}

void SceneGraph::Update(nvrhi::ICommandList* commandList)
{
	UpdateLights(commandList);

	eastl::vector<RE::BSTriShape*> destroyedMeshes; 
	{
		std::scoped_lock lock(m_MeshDestroyMutex);
		destroyedMeshes = eastl::move(m_DestroyedMeshes);
	}

	// Unconditional release for now, needs to be revised for buffering/frames in flight support
	for (auto destroyedMesh: destroyedMeshes)
	{
		// Might be unecessary since direct meshes is only modifier below, and this function runs on the main thread
		std::scoped_lock lock(m_MeshMutex);

		auto it = m_DirectMeshes.find(destroyedMesh);
		if (it == m_DirectMeshes.end())
			continue;

		auto* mesh = it->second.get();

		// Remove from its cluster (owner used as key only, never dereferenced).
		RemoveMeshFromCluster(mesh, mesh->GetOwner());

		m_DirectMeshes.erase(it);
	}

	auto shadowSceneNode = Util::Adapter::GetShaderManagerState().shadowSceneNode[0];

	// Hardcoded for now
	const bool skipClustering = false;

	m_NumMeshes = 0;
	m_NumInstances = 0;

	Util::Traversal::ScenegraphTriShapes(shadowSceneNode, [&](RE::BSTriShape* bsTriShape, bool hidden, RE::TESObjectREFR* ownerRefr) -> CESEAdapter::RE::BSVisitControl {
		if (bsTriShape->GetType().none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kDynamicTriShape))
			return CESEAdapter::RE::BSVisitControl::kContinue;

		const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);

		auto* shaderProperty = geometryData.shaderProperty;
		if (!shaderProperty) 
			return CESEAdapter::RE::BSVisitControl::kContinue;

		const bool isLightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProperty) != nullptr;
		const bool isEffectShader = netimmerse_cast<RE::BSEffectShaderProperty*>(shaderProperty) != nullptr;
		const bool isWaterShader = netimmerse_cast<RE::BSWaterShaderProperty*>(shaderProperty) != nullptr;
		const bool isTreeLODShader = netimmerse_cast<RE::BSDistantTreeShaderProperty*>(shaderProperty) != nullptr;
		const bool isGrassShader = netimmerse_cast<RE::BSGrassShaderProperty*>(shaderProperty) != nullptr;

		if (!isLightingShader && !isEffectShader && !isWaterShader && !isTreeLODShader && !isGrassShader)
			return CESEAdapter::RE::BSVisitControl::kContinue;

		const bool skinned = !geometryData.rendererData && geometryData.skinInstance && geometryData.skinInstance->skinPartition && geometryData.skinInstance->skinPartition->numPartitions > 0;

		if (!skinned) {
			const auto& trishapeData = bsTriShape->GetTrishapeRuntimeData();
			if (trishapeData.vertexCount == 0 || trishapeData.triangleCount == 0) {
				logger::warn("BSTriShape \"{}\" has either vertex ({}) or triangle ({}) count of 0, skipping.", bsTriShape->name, trishapeData.vertexCount, trishapeData.triangleCount);
				return CESEAdapter::RE::BSVisitControl::kContinue;
			}
		}
		
		const auto rendererData = skinned ? geometryData.skinInstance->skinPartition->partitions[0].buffData : geometryData.rendererData;
		if (!rendererData) {
			logger::warn("BSTriShape \"{}\" has no renderer data, skipping.", bsTriShape->name);
			return CESEAdapter::RE::BSVisitControl::kContinue;
		}

		{
			// When clustering is skipped, force a null owner so every mesh lands in its own orphan cluster.
			RE::TESObjectREFR* const clusterOwner = skipClustering ? nullptr : ownerRefr;

			eastl::shared_ptr<BaseMesh> mesh;
			{
				std::scoped_lock lock(m_MeshMutex);

				auto it = m_DirectMeshes.find(bsTriShape);
				if (it != m_DirectMeshes.end())
					mesh = it->second;
			}

			// If exists - update owner/visibility/data state (dirty flags live inside the mesh), else - create if visible 
			if (mesh) {
				// SetOwner returns true if the owner changed; re-bucket into the new cluster (key compare only).
				if (mesh->SetOwner(clusterOwner)) {
					RemoveMeshFromCluster(mesh.get(), mesh->GetPrevOwner());
					GetOrCreateCluster(clusterOwner, bsTriShape)->AddMember(mesh);
				}

				mesh->SetHidden(hidden);

				if (!hidden) {
					// CPU-side change detection only; the GPU upload happens in the TLAS pass (cluster-driven).
					// SkinnedMesh::UpdateData recomputes bone matrices while the trishape + skin instance are alive.
					const bool poseAdvanced = mesh->UpdateData();

					// Queue skinned/dynamic meshes for the GPU skinning pass.
					if (auto* skinnedMesh = mesh->AsSkinnedMesh()) {
						DirtyFlags skinningFlags = DirtyFlags::None;
						if (poseAdvanced)
							skinningFlags |= DirtyFlags::Skin;
						if (mesh->GetDirtyFlags().any(DirtyFlags::Vertex))
							skinningFlags |= DirtyFlags::Vertex;

						if (skinningFlags != DirtyFlags::None) {
							if (auto* skinningPass = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode()->GetPass<Pass::Skinning>())
								skinningPass->QueueUpdate(skinningFlags, skinnedMesh);
						}
					}

					// Capture transforms while the owner/trishape are alive; cluster consumes cached values later.
					UpdateMeshTransforms(mesh.get(), clusterOwner, bsTriShape);
				}
			}
			else if (!hidden) {
				if (auto created = BaseMesh::Create(bsTriShape, commandList)) {
					created->SetOwner(clusterOwner);

					eastl::shared_ptr<BaseMesh> registered;
					{
						std::scoped_lock lock(m_MeshMutex);
						auto [it, inserted] = m_DirectMeshes.emplace(bsTriShape, created);
						if (inserted)
							registered = it->second;
					}

					if (registered) {
						GetOrCreateCluster(clusterOwner, bsTriShape)->AddMember(registered);
						UpdateMeshTransforms(registered.get(), clusterOwner, bsTriShape);
					}
				}
			}
		}

		return CESEAdapter::RE::BSVisitControl::kContinue;
	});

	// Drop clusters whose meshes were all destroyed this frame.
	for (auto it = m_OwnerClusters.begin(); it != m_OwnerClusters.end(); ) {
		if (it->second->Empty())
			it = m_OwnerClusters.erase(it);
		else
			++it;
	}

	for (auto it = m_OrphanClusters.begin(); it != m_OrphanClusters.end(); ) {
		if (it->second->Empty())
			it = m_OrphanClusters.erase(it);
		else
			++it;
	}

	// Populate per-instance + per-geometry data for shader-side geometry lookup, in the same
	// owner-then-orphan / member order the TLAS and BLAS use (so InstanceID()/GeometryIndex() align).
	auto appendInstance = [&](BLASCluster* cluster) {
		if (m_NumInstances >= Constants::NUM_INSTANCES_MAX)
			return;

		InstanceData instance;
		if (cluster->GetData(m_MeshData.data(), m_NumMeshes, instance)) {
			cluster->SetInstanceIndex(m_NumInstances);
			m_InstanceData[m_NumInstances] = instance;
			m_NumInstances++;
		}
	};

	for (auto& [owner, cluster] : m_OwnerClusters)
		appendInstance(cluster.get());

	for (auto& [bsTriShape, cluster] : m_OrphanClusters)
		appendInstance(cluster.get());

	/*eastl::array<uint8_t, Constants::INSTANCE_LIGHTS_MAX> lights;

	m_Instances.ApplyChanges();

	m_Instances.Write([&](eastl::unique_ptr<Instance>& instance) {
		instance->Update(m_NumInstances);

		if (instance->IsHidden())
			return Iterator::Continue;

		bool isPlayer = Util::IsPlayerFormID(instance->m_FormID);

		// Update if applicable, and queue skinning/dynamic update
		instance->model->Update(instance->m_Node, isPlayer, commandList);

		// Get mesh data
		auto params = instance->model->GetData(m_MeshData.data(), m_NumMeshes);

		// No visible meshes in instance
		instance->SetHiddenModel(params.hidden);

		// Post update since these states are set by update itself
		if (instance->SkipAS())
			return Iterator::Continue;

		uint8_t numLights = 0u;

		for (auto& [bsLight, light] : m_Lights)
		{
			if (light.m_Instances.find(instance.get()) == light.m_Instances.end())
				continue;

			lights[numLights] = light.m_Index;
			numLights++;

			if (numLights >= Constants::INSTANCE_LIGHTS_MAX) {
				logger::error("SceneGraph::Update - Number of lights per instance of {} exceeds the maximum of {}", numLights, Constants::INSTANCE_LIGHTS_MAX);
				break;
			}
		}

		m_InstanceData[m_NumInstances] = {
			instance->m_Transform,
			instance->m_PrevTransform,
			InstanceLightData(lights.data(), numLights),
			params.firstMeshID,
			params.numMeshes,
			instance->GetAlpha()
		};

		m_NumInstances++;
		return Iterator::Continue;
	});*/

	if (m_NumMeshes >= Constants::NUM_MESHES_MAX)
		logger::critical("SceneGraph::Update - Number of meshes of {} exceeds the maximum of {}", m_NumMeshes, Constants::NUM_MESHES_MAX);

	if (m_NumMeshes > 0)
		commandList->writeBuffer(m_MeshBuffer, m_MeshData.data(), m_NumMeshes * sizeof(MeshData));

	if (m_NumInstances >= Constants::NUM_INSTANCES_MAX)
		logger::critical("SceneGraph::Update - Number of instances of {} exceeds the maximum of {}", m_NumInstances, Constants::NUM_INSTANCES_MAX);

	if (m_NumInstances > 0)
		commandList->writeBuffer(m_InstanceBuffer, m_InstanceData.data(), m_NumInstances * sizeof(InstanceData));
}

void SceneGraph::ClearDirtyStates()
{
	m_Instances.Read([&](auto& instance) {
		instance->ClearDirtyState();
		return Iterator::Continue;
	});
}

bool SceneGraph::TryMaintenanceRebuild(uint64_t frameIndex)
{
	if (frameIndex != m_LastMaintenanceFrame) {
		m_LastMaintenanceFrame = frameIndex;
		m_MaintenanceRebuildsThisFrame = 0;
	}

	if (m_MaintenanceRebuildsThisFrame < Constants::MAX_BLAS_MAINTENANCE_REBUILDS_PER_FRAME) {
		m_MaintenanceRebuildsThisFrame++;
		return true;
	}

	return false;
}

BLASCluster* SceneGraph::GetOrCreateCluster(RE::TESObjectREFR* owner, RE::BSTriShape* bsTriShape)
{
	if (owner) {
		auto& slot = m_OwnerClusters[owner];
		if (!slot)
			slot = eastl::make_unique<BLASCluster>(owner);
		return slot.get();
	}

	auto& slot = m_OrphanClusters[bsTriShape];
	if (!slot)
		slot = eastl::make_unique<BLASCluster>(nullptr);
	return slot.get();
}

void SceneGraph::RemoveMeshFromCluster(BaseMesh* mesh, RE::TESObjectREFR* owner)
{
	// owner is used as a key only (never dereferenced); orphan meshes are keyed by their trishape.
	if (owner) {
		auto it = m_OwnerClusters.find(owner);
		if (it != m_OwnerClusters.end())
			it->second->RemoveMember(mesh);
	}
	else {
		auto it = m_OrphanClusters.find(mesh->GetTriShape());
		if (it != m_OrphanClusters.end())
			it->second->RemoveMember(mesh);
	}
}

void SceneGraph::UpdateMeshTransforms(BaseMesh* mesh, RE::TESObjectREFR* owner, RE::BSTriShape* bsTriShape)
{
	// Owner + trishape are alive here (traversal), so it's safe to read their world transforms.
	// Orphan meshes (no owner) use their own world, giving an identity local-to-owner.
	RE::NiTransform ownerWorld = bsTriShape->world;
	if (owner)
		if (auto* node = owner->Get3D())
			ownerWorld = node->world;

	// Bake the per-member local-to-owner into the mesh's geometry descs.
	const auto localToOwner = Util::Math::ComputeLocalToRoot(ownerWorld.Invert(), bsTriShape->world);
	mesh->SetLocalToOwner(localToOwner);

	// Cache the owner-world transform on the cluster for the TLAS instance (no later deref).
	float3x4 instanceTransform;
	XMStoreFloat3x4(&instanceTransform, Util::Math::GetXMFromNiTransform(ownerWorld));

	GetOrCreateCluster(owner, bsTriShape)->SetInstanceTransform(instanceTransform);
}

void SceneGraph::BuildClusters(nvrhi::ICommandList* commandList)
{
	std::scoped_lock lock(m_MeshMutex);

	// Each cluster pulls dirty state from its members, uploads pending dynamic buffers, and builds/refits.
	for (auto& [owner, cluster] : m_OwnerClusters)
		cluster->BuildUpdate(commandList, this);

	for (auto& [bsTriShape, cluster] : m_OrphanClusters)
		cluster->BuildUpdate(commandList, this);
}

void SceneGraph::CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root)
{
	if (!root) {
		logger::warn("SceneGraph::CreateModel - NULL root object for model: {}", model ? model : "unknown");
		return;
	}

#if defined(SKYRIM)
	// TODO: Proper Model transform update, this whole section feels like hack
	const REL::Relocation<const RE::NiRTTI*> rtti{ NiRTTI(NiMultiTargetTransformController) };
	auto* controller = reinterpret_cast<RE::NiMultiTargetTransformController*>(root->GetController(rtti.get()));
	
	if (controller) {
		eastl::hash_set<RE::NiNode*> parents;
		eastl::hash_set<RE::NiAVObject*> targets;

		uint32_t createModels = 0;

		for (uint16_t i = 0; i < controller->numInterps; i++) {
			auto* target = controller->targets[i];

			if (!target)
				continue;

			auto [it, emplaced] = targets.emplace(target);
			if (target->parent)
				parents.emplace(target->parent);

			if (!emplaced)
				continue;

			createModels += CreateModelInternal(form, std::format("{}_{}", model, target->name.c_str()).c_str(), target);
		}

		for (auto* parent : parents) {
			for (auto& child : Util::Adapter::GetChildren(parent)) {
				if (!child)
					continue;

				if (targets.find(child.get()) != targets.end())
					continue;

				createModels += CreateModelInternal(form, std::format("{}_{}_{}", model, child->name.c_str(), child->parentIndex).c_str(), child.get());
			}
		}

		if (createModels > 0)
			return;
	}
#endif

	CreateModelInternal(form, model, root);
}

void SceneGraph::CreateActorModel(RE::Actor* actor, RE::NiAVObject* root, bool firstPerson)
{
	auto name = std::format("{}{}_{:0X}", actor->GetDisplayFullName(), firstPerson ? "_1stPerson" : "", actor->GetFormID());

	auto* biped = actor->GetBiped(firstPerson).get();

	logger::debug("SceneGraph::CreateActorModel - {}", name);

	if (biped) {
		eastl::vector<eastl::unique_ptr<Mesh>> meshes;
		eastl::vector<Mesh*> faceMeshes;
		eastl::array<eastl::vector<Mesh*>, static_cast<int32_t>(RE::BIPED_OBJECT::kTotal)> bipedMeshes;

		auto createAppendMeshes = [&](RE::TESForm* form, RE::NiAVObject* object, int i = -1) {
			logger::debug("Appending {}: {}", magic_enum::enum_name(form->GetFormType()), object->name);

			for (auto& mesh : CreateMeshes(object, form))
			{
				if (i == -1)
					faceMeshes.push_back(mesh.get());
				else
					bipedMeshes[i].push_back(mesh.get());

				meshes.push_back(eastl::move(mesh));
			}
		};

		if (!firstPerson)
			if (auto* headNode = actor->GetFaceNodeSkinned())
#if defined(SKYRIM)
				createAppendMeshes(actor, headNode);
#elif defined(FALLOUT4)
				createAppendMeshes(actor, reinterpret_cast<RE::NiAVObject*>(headNode));
#endif

		for (uint32_t i = 0; i < static_cast<int32_t>(RE::BIPED_OBJECT::kTotal); i++)
		{
			auto& object = Util::Adapter::GetBipedObjects(biped)[i];

			auto objectItem = Util::Adapter::GetBipedObjectItem(object);

			if (!objectItem)
				continue;

			if (!object.partClone)
				continue;

			logger::debug("\tSceneGraph::CreateActorModel - {}", magic_enum::enum_name(static_cast<RE::BIPED_OBJECT>(i)));
			createAppendMeshes(objectItem, object.partClone.get(), i);
		}

		auto object = actor->Get3D(firstPerson);

		if (auto* model = CommitModel(name.c_str(), object, actor, meshes)) {
			AddInstance(actor->GetFormID(), object, model);
			m_Actors.try_emplace(actor->GetFormID(), ActorReference(actor, firstPerson, faceMeshes, bipedMeshes));
		}
	}
	else {
		Util::Traversal::ScenegraphFadeNodes(root, [&](RE::BSFadeNode* fadeNode) -> CESEAdapter::RE::BSVisitControl {
			const bool isRoot = (fadeNode == root);

			auto fadeNodeName = std::format("{}.{}", name, fadeNode->name.c_str());
			CreateModelInternal(actor, isRoot ? name.c_str() : fadeNodeName.c_str(), fadeNode);

			return CESEAdapter::RE::BSVisitControl::kContinue;
		});
	}
}

ActorReference* SceneGraph::GetActorRefr(RE::FormID a_formID)
{
	auto it = m_Actors.find(a_formID);

	if (it == m_Actors.end())
		return nullptr;

	return &it->second;
}

void SceneGraph::CreateLandModel(RE::TESObjectLAND* land)
{
	auto* cell = land->parentCell;

	if (!Util::Adapter::IsExteriorCell(cell))
		return;

	auto* exteriorData = Util::Adapter::GetCellExteriorData(cell);

	auto* loadedData = land->loadedData;

	if (!loadedData || !loadedData->mesh)
		return;

	logger::debug("SceneGraph::CreateLandModel - {}", std::format("Landscape_{}_{}", exteriorData->cellX, exteriorData->cellY).c_str());

	for (uint i = 0; i < 4; i++) {
		auto mesh = loadedData->mesh[i];

		if (!mesh) {
			logger::warn("SceneGraph::CreateLandModel - Mesh [{}] is nullptr", i);
			continue;
		}

		CreateModelInternal(land, std::format("Land_{:0X}_{}_{}_Quad_{}", land->GetFormID(), exteriorData->cellX, exteriorData->cellY, i).c_str(), mesh);
	}
}

void SceneGraph::CreateWaterModel(RE::TESWaterForm* water, RE::NiAVObject* object)
{
	if (!water || !object)
		return;

	if (m_WaterInstances.contains(object))
		return;

	auto path = std::format("Water_0x{:08X}", reinterpret_cast<uintptr_t>(object));

	logger::debug("SceneGraph::CreateWaterModel - FormID 0x{:08X}, {}", water->GetFormID(), path.c_str());

	// Creates all meshes, one for each valid BSGeometry found in the NiAVObject hierarchy
	auto meshes = CreateMeshes(object, water);

	if (auto* model = CommitModel(path.c_str(), object, water, meshes)) {
		if (auto* instance = AddInstanceImpl(object, model, 0))
			m_WaterInstances.emplace(object, instance);
	}
}

#if defined(SKYRIM)
void SceneGraph::CreateGrassModel(RE::BGSGrassManager* a_grassManager, RE::CreateGrassParams* a_createGrassParams, uint32_t numInstances)
{
	auto* grassParams = a_createGrassParams->grassParam;

	auto* grassForm = RE::TESForm::LookupByID<RE::TESGrass>(grassParams->grassFormID);
	if (!grassForm || grassForm->model.empty())
		return;

	logger::debug("SceneGraph::CreateGrassModel - Land: {:0X}, Quad: {}, Model: {}, Instances: {}", a_createGrassParams->land->GetFormID(), a_createGrassParams->quad, grassParams->modelName, numInstances);

	// Generate the key exactly how its done by the engine
	auto exteriorData = a_createGrassParams->land->parentCell->GetCoordinates();
	auto keyX = exteriorData->cellX / 12;
	auto keyY = exteriorData->cellY / 12;

	auto grassKey = RE::GrassTypeKey(grassParams->grassFormID, static_cast<int16_t>(keyX), static_cast<int16_t>(keyY));

	// The hash map type used by clib is incorrect, cast to the correct type before attempting to use it
	auto& grassTypes = *reinterpret_cast<RE::BSTCustomHashMap<RE::GrassTypeKey, RE::GrassType*>*>(&a_grassManager->unk10);

	auto it = grassTypes.find(grassKey);
	if (it == grassTypes.end()) {
		logger::warn("\tSceneGraph::CreateGrassModel - Grass Type not found for ({:0X}, [{}, {}])", grassParams->grassFormID, keyX, keyY);
		return;
	}

	auto* grassShape = it->second->typeShape;
	if (!grassShape) {
		logger::warn("\tSceneGraph::CreateGrassModel - Grass Type is nullptr");
		return;
	}

	auto modelName = eastl::string(grassForm->model.c_str());

	Model* model = nullptr;
	{
		std::scoped_lock lock(m_ModelMutex);
		if (auto modelIt = m_Models.find(modelName); modelIt != m_Models.end())
			model = modelIt->second.get();
	}

	if (!model) {
		auto meshes = CreateMeshes(grassShape, grassForm);
		model = CommitModel(modelName.c_str(), grassShape, grassForm, meshes);
	}

	if (!model) {
		logger::warn("SceneGraph::CreateGrassModel - Grass model {} is null", modelName);
		return;
	}

	auto& grassInstance = m_GrassInstances[grassKey];

	if (grassInstance.m_Instances.size() > 10000)
		return;

	auto instanceData = reinterpret_cast<GrassReference::InstanceData*>(a_grassManager->instanceData);

	for (size_t i = 0; i < numInstances; i++)
	{
		auto instanceDataLocal = instanceData[i];

		auto instance = eastl::make_unique<GrassInstance>(instanceDataLocal, grassParams->grassFormID, grassShape, model);
		instance->model->AddRef();

		grassInstance.m_Instances.push_back(instance.get());
		grassInstance.m_InstanceData.push_back(instanceDataLocal);

		m_Instances.Add(eastl::move(instance));
	}
}
#endif

bool SceneGraph::CreateLODModel(RE::BGSTerrainBlock* block)
{
	if (!m_TerrainLODInstances.contains(block)) {
		CreateLODModelImpl(block, Mesh::Type::LandLOD);
		return false;
	}

	return true;
}

bool SceneGraph::CreateLODModel(RE::BGSObjectBlock* block)
{
	if (!m_ObjectLODInstances.contains(block)) {
		CreateLODModelImpl(block, Mesh::Type::ObjectLOD);
		return false;
	}

	return true;
}

#if defined(SKYRIM)
bool SceneGraph::CreateLODModel(RE::BGSDistantTreeBlock* block)
{
	if (m_TreeLODInstances.contains(block))
		return true;

	auto [it, inserted] = m_TreeLODInstances.emplace(block, eastl::make_unique<TreeLODBlockReference>(block));
	auto blockRefr = it->second.get();

	for (const auto& group: block->treeGroups)
	{
		if (!group->geometry) {
			logger::warn("SceneGraph::CreateLODModel - Tree lod group has no geometry");
			continue;
		}

		auto* geometry = group->geometry.get();

		auto modelNameTmp = std::format("TreeLOD_{}", group->treeType);
		auto modelName = eastl::string(modelNameTmp.c_str());

		Model* model = nullptr;
		{
			std::scoped_lock lock(m_ModelMutex);
			if (auto modelIt = m_Models.find(modelName); modelIt != m_Models.end())
				model = modelIt->second.get();
		}

		if (!model) {
			auto meshes = CreateMeshes(geometry, nullptr);
			model = CommitModel(modelName.c_str(), geometry, nullptr, meshes);
		}

		if (!model)
			logger::warn("SceneGraph::CreateLODModel - Tree lod model {} is null", group->treeType);

		for (auto& instanceData: group->instances)
		{
			auto* instanceDataPtr = &instanceData;

			auto instance = eastl::make_unique<TreeLODInstance>(instanceDataPtr, geometry, model);
			instance->model->AddRef();

			blockRefr->AddInstance(instance.get());
			blockRefr->AddTreeInstanceData(instanceDataPtr);

			m_Instances.Add(eastl::move(instance));
		}
	}

	return false;
}
#endif

template <typename T>
void SceneGraph::CreateLODModelImpl(T* block, Mesh::Type type)
{
	auto node = block->chunk;

	if (!node) {
		logger::warn("SceneGraph::CreateLODModelImpl - Chunk is nullptr for {}", magic_enum::enum_name(type));
		return;
	}

	logger::debug("SceneGraph::CreateLODModel - {}", node->name.c_str());

	auto rootWorldInverse = node->world.Invert();

	Util::Traversal::ScenegraphRTGeometries(node, nullptr, [&](RE::BSGeometry* pGeometry)->CESEAdapter::RE::BSVisitControl {
		if (type == Mesh::Type::LandLOD && pGeometry->parent && pGeometry->parent->name == "WATER")
			return CESEAdapter::RE::BSVisitControl::kContinue;

#if defined(SKYRIM)
		if (pGeometry->GetType().none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kSubIndexTriShape)) {
			logger::warn("SceneGraph::CreateLODModelImpl - Unsupported geometry type: {} for {}", magic_enum::enum_name(pGeometry->GetType().get()), magic_enum::enum_name(type));
			return CESEAdapter::RE::BSVisitControl::kContinue;
		}

		logger::debug("\t{}: {}, {}", magic_enum::enum_name(pGeometry->GetType().get()), pGeometry->name.c_str(), Util::Math::Float3(pGeometry->world.translate));
#elif defined(FALLOUT4)
		if (pGeometry->type != 3 && pGeometry->type != 8) {
			logger::warn("SceneGraph::CreateLODModelImpl - Unsupported geometry type: {} for {}", static_cast<int>(pGeometry->type), magic_enum::enum_name(type));
			return CESEAdapter::RE::BSVisitControl::kContinue;
		}

		logger::debug("\t{}: {}", static_cast<int>(pGeometry->type), pGeometry->name.c_str());
#endif

		const auto& geometryRuntimeData = Util::Adapter::GetGeometryRuntimeData(pGeometry);

		if (!geometryRuntimeData.shaderProperty)
			return CESEAdapter::RE::BSVisitControl::kContinue;

		auto* triShapeRD = geometryRuntimeData.rendererData;

		if (!triShapeRD) {
			logger::info("\tInvalid LOD Render Data");
			return CESEAdapter::RE::BSVisitControl::kContinue;
		}

		eastl::vector<eastl::unique_ptr<Mesh>> meshes;

		float3x4 localToRoot = Util::Math::ComputeLocalToRoot(rootWorldInverse, pGeometry->world);

		auto triShape = netimmerse_cast<RE::BSTriShape*>(pGeometry);

#if defined(SKYRIM)
		const auto& triShapeRuntime = triShape->GetTrishapeRuntimeData();
		const uint32_t vertexCount = triShapeRuntime.vertexCount;
		const uint32_t triangleCount = triShapeRuntime.triangleCount;
#elif defined(FALLOUT4)
		const uint32_t vertexCount = triShape->numVertices;
		const uint32_t triangleCount = triShape->numTriangles;
#endif

		const char* name = pGeometry->name.c_str();

#if defined(SKYRIM)
		if (pGeometry->GetType().all(RE::BSGeometry::Type::kSubIndexTriShape)) {
#elif defined(FALLOUT4)
		if (pGeometry->type == 8) {
#endif
			auto* subIndexTriShape = netimmerse_cast<RE::BSSubIndexTriShape*>(pGeometry);

			if (subIndexTriShape) {
				stl::enumeration<Mesh::Flags> flags = Mesh::Flags::SubIndex;
				auto vertexData = Mesh::BuildVertices(flags, pGeometry, triShapeRD, vertexCount, 0);
				auto triangleData = Mesh::BuildTriangles(flags.get(), triShapeRD, triangleCount);

#if defined(SKYRIM)
				auto& runtimeData = subIndexTriShape->GetSubIndexedTrishapeRuntimeData();
				
				logger::debug("SubIndexTriShape - 0x{:08X} - Triangles: {}", reinterpret_cast<uintptr_t>(subIndexTriShape), triangleCount);

				logger::debug("SubIndexTriShape - Segments: {}, UnkSegments: {}, Unk170: {}, NonSegmented: {}",
					runtimeData.numSegments, runtimeData.unkSegCount, runtimeData.unk170, runtimeData.nonSegmented);

				for (size_t i = 0; i < runtimeData.numSegments; i++)
				{
					auto& segment = runtimeData.segmentData[i];

					// The first segment contains all triangles (it is the equivalent of all other segments combined)
					if (i == 0 && runtimeData.numSegments > 1) {
						continue;
					} else if (segment.unkTriCount == 0 && segment.numTris == 0) // The first segments 'unkTriCount' is always 0, if not the first segment and the counts are 0, skip
						continue;

					logger::debug("\tSegment[{}]: Index: {}, UnkTriCount: {}, UnkFlags: 0x{:08X}, NumTris: {}, Flags: 0x{:08X}",
						i, segment.index, segment.unkTriCount, segment.unkFlags, segment.numTris, segment.flags);

					auto identifier = static_cast<uint32_t>((segment.index / 3) << 16) | segment.numTris;

					auto mesh = eastl::make_unique<Mesh>(RE::FormType{}, type, flags.get(), name, pGeometry, localToRoot, identifier);

					// Copy triangles to segment triangles
					Mesh::TriangleData segmentTriData{};
					{
						auto startTriangle = segment.index / 3;
						auto numTriangles = segment.numTris;

						segmentTriData.triangles.resize(segment.numTris);
						memcpy(segmentTriData.triangles.data(), triangleData.triangles.data() + startTriangle, numTriangles * sizeof(Triangle));

						segmentTriData.count = numTriangles;
					}

					mesh->BuildMesh(vertexData, segmentTriData, triShapeRD->vertexDesc);
					mesh->BuildMaterial(geometryRuntimeData, 0);

					meshes.push_back(eastl::move(mesh));
				}
#elif defined(FALLOUT4)
				auto mesh = eastl::make_unique<Mesh>(RE::FormType{}, type, flags.get(), name, pGeometry, localToRoot);
				mesh->BuildMesh(triShapeRD, vertexCount, triangleCount, 0);
				mesh->BuildMaterial(geometryRuntimeData, 0);
				meshes.push_back(eastl::move(mesh));
#endif
			}
		}
		else {
			auto mesh = eastl::make_unique<Mesh>(RE::FormType{}, type, Mesh::Flags::None, name, pGeometry, localToRoot);

			mesh->BuildMesh(triShapeRD, vertexCount, triangleCount, 0);
			mesh->BuildMaterial(geometryRuntimeData, 0);

			meshes.push_back(eastl::move(mesh));
		}

		auto path = std::format("{}_0x{:08X}", pGeometry->name.c_str(), reinterpret_cast<uintptr_t>(pGeometry));

		if (auto* model = CommitModel(path.c_str(), pGeometry, nullptr, meshes))
			AddInstance(block, pGeometry, model);

		return CESEAdapter::RE::BSVisitControl::kContinue;
	});
}

void SceneGraph::ActorEquip(RE::Actor* a_actor, RE::TESForm* a_form, RE::NiAVObject* a_object, eastl::vector<Mesh*>& a_meshes, bool firstPerson)
{
	if (!a_form)
		return;

	if (!a_object)
		return;

	auto it = m_InstancesFormIDs.find(a_actor->GetFormID());

	if (it == m_InstancesFormIDs.end())
		return;

	auto meshes = CreateMeshes(a_object, a_form);

	if (meshes.empty()) {
		logger::info("SceneGraph::ActorEquip - Empty meshes for {}", Util::Adapter::GetName(a_actor));
		return;
	}

	Model* targetModel = nullptr;

	for (const auto& instance : it->second) {
		if (!instance || !instance->model)
			continue;

		if (instance->model->m_FirstPerson == firstPerson) {
			targetModel = instance->model;
			break;
		}
	}

	if (!targetModel) {
		logger::info("SceneGraph::ActorEquip - Instance not found for {}, First Person: {}", Util::Adapter::GetName(a_actor), firstPerson);
		return;
	}

	for (const auto& mesh: meshes)
		a_meshes.push_back(mesh.get());

	targetModel->AppendMeshes(this, meshes);
}

void SceneGraph::ActorUnequip(RE::Actor* a_actor, const eastl::vector<Mesh*>& a_meshes, bool firstPerson)
{
	auto it = m_InstancesFormIDs.find(a_actor->GetFormID());

	if (it == m_InstancesFormIDs.end())
		return;

	for (const auto& instance : it->second) {
		if (instance->model->m_FirstPerson == firstPerson) {
			instance->model->RemoveMeshes(a_meshes);
			break;
		}
	}
}

void SceneGraph::ReleaseTexture(RE::BSGraphics::Texture* texture)
{
	m_TextureManager->ReleaseTexture(texture);
}

void SceneGraph::ReleaseModel(const Model* model)
{
	std::scoped_lock modelLock(m_ModelMutex);

	auto it = m_Models.find(model->m_Name);
	if (!(model->m_Flags & Model::Flags::BuffersUploaded) || !(model->m_Flags & Model::Flag::BLASBuilt))
	{
		std::scoped_lock releaseLock(m_ModelReleaseMutex);
		m_ReleasedModels.push_back(eastl::move(it->second));
		logger::debug("SceneGraph::ReleaseModel - Model {} has pending command list actions, released will be delayed until done.", model->m_Name);
	}

	m_Models.erase(it);
}

void SceneGraph::ReleaseWaterInstance(RE::NiAVObject* node)
{
	auto it = m_WaterInstances.find(node);
	if (it == m_WaterInstances.end())
		return;

	m_WaterInstances.erase(it);

	// Removes the original instance
	m_Instances.Remove(InstanceManager::RemoveParams(it->second, true));
}

void SceneGraph::ReleaseInstances(eastl::vector<Instance*>& instances, bool releaseModel)
{
	for (auto* instance : instances) {
		m_Instances.Remove(InstanceManager::RemoveParams(instance, releaseModel));
	}
}

void SceneGraph::ReleaseInstances(RE::TESForm* form, bool releaseModel)
{
	auto formID = form->GetFormID();

	if (releaseModel) {
		m_Actors.erase(formID);
	}

	auto it = m_InstancesFormIDs.find(formID);

	if (it == m_InstancesFormIDs.end())
		return;

	ReleaseInstances(it->second, releaseModel);

	m_InstancesFormIDs.erase(it);
}

void SceneGraph::ReleaseInstances(RE::BGSTerrainBlock* block)
{
	m_TerrainLODInstances.erase(block);
}

void SceneGraph::ReleaseInstances(RE::BGSObjectBlock* block)
{
	m_ObjectLODInstances.erase(block);
}

#if defined(SKYRIM)
void SceneGraph::ReleaseInstances(RE::BGSDistantTreeBlock* block)
{
	m_TreeLODInstances.erase(block);
}
#endif

void SceneGraph::SetInstanceDetached(RE::TESForm* form, bool detached)
{
	auto instanceFormIDsIt = m_InstancesFormIDs.find(form->GetFormID());

	if (instanceFormIDsIt == m_InstancesFormIDs.end())
		return;

	logger::debug("SceneGraph::SetInstanceDetached - Detaching {}, 0x{:08X}", detached, form->GetFormID());

	for (auto& instance : instanceFormIDsIt->second) {
		logger::debug("	SceneGraph::SetInstanceDetached - {}", instance->model->m_Name.c_str());

		instance->SetDetached(detached);
	}
}

eastl::vector<eastl::unique_ptr<Mesh>> SceneGraph::CreateMeshes(RE::NiAVObject* object, RE::TESForm* form)
{
	auto formType = form ? form->GetFormType() : RE::FormType{};
	auto baseFormType = formType;

	if (form) {
		if (auto* refr = Util::Adapter::AsReference(form)) {
			if (auto* baseObject = Util::Adapter::GetBaseObject(refr))
				baseFormType = baseObject->GetFormType();
		}
	}

	auto rootWorldInverse = object->world.Invert();

	const bool isRootOrigin = object->world.translate == Util::Adapter::GetNiPoint3Zero();

	eastl::vector<eastl::unique_ptr<Mesh>> meshes;

	if (object->HasExtraData("HDT Skinned Mesh Physics Object"))
		return meshes;

	Util::Traversal::ScenegraphRTGeometries(object, nullptr, [&](RE::BSGeometry* pGeometry)->CESEAdapter::RE::BSVisitControl {
		const char* name = pGeometry->name.c_str();

		if (Util::Geometry::IsBlocklisted(name))
			return CESEAdapter::RE::BSVisitControl::kContinue;

		logger::trace("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - {}", name);

#if defined(SKYRIM)
		const auto& geometryType = pGeometry->GetType();

		if (geometryType.none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kDynamicTriShape, RE::BSGeometry::Type::kMultiStreamInstanceTriShape)) {
			logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Unsupported Geometry: {} for {}", magic_enum::enum_name(geometryType.get()), name);
			return CESEAdapter::RE::BSVisitControl::kContinue;
		}
#elif defined(FALLOUT4)
		const uint8_t geometryType = pGeometry->type;

		if (geometryType != 3 && geometryType != 4 && geometryType != 10) {
			logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Unsupported Geometry: {} for {}", static_cast<int>(geometryType), name);
			return CESEAdapter::RE::BSVisitControl::kContinue;
		}
#endif

		const auto& geometryRuntimeData = Util::Adapter::GetGeometryRuntimeData(pGeometry);

		auto* shaderProperty = geometryRuntimeData.shaderProperty;

		if (!shaderProperty) {
			logger::debug("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - No Effect");
			return CESEAdapter::RE::BSVisitControl::kContinue;
		}

		bool isLightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProperty) != nullptr;
#if defined(SKYRIM)
		bool isEffectShader = netimmerse_cast<RE::BSEffectShaderProperty*>(shaderProperty) != nullptr;
		bool isWaterShader = netimmerse_cast<RE::BSWaterShaderProperty*>(shaderProperty) != nullptr;
		bool isTreeLODShader = netimmerse_cast<RE::BSDistantTreeShaderProperty*>(shaderProperty) != nullptr;
		bool isGrassShader = netimmerse_cast<RE::BSGrassShaderProperty*>(shaderProperty) != nullptr;

		// Only lighting and effect shader for now
		if (!isLightingShader && !isEffectShader && !isWaterShader && !isTreeLODShader && !isGrassShader) {
			logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Unsupported shader type: {}", shaderProperty->GetRTTI()->name);
			return CESEAdapter::RE::BSVisitControl::kContinue;
		}

		// Ignore effect shader with alpha blend
		if (isEffectShader && geometryRuntimeData.alphaProperty)
			if (geometryRuntimeData.alphaProperty->GetAlphaBlending())
				return CESEAdapter::RE::BSVisitControl::kContinue;
#elif defined(FALLOUT4)
		if (!isLightingShader) {
			logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Unsupported shader type: {}", shaderProperty->GetRTTI()->name);
			return CESEAdapter::RE::BSVisitControl::kContinue;
		}
#endif

		auto flags = Mesh::Flags::None;

		// Landscape needs special handling of triangles
#if defined(SKYRIM)
		if (baseFormType == RE::FormType::Land)
			flags |= Mesh::Flags::Landscape;
		else if (baseFormType == RE::FormType::Water)
			flags |= Mesh::Flags::Water;
#elif defined(FALLOUT4)
		if (baseFormType == RE::FormType_Land)
			flags |= Mesh::Flags::Landscape;
		else if (baseFormType == RE::FormType_Water)
			flags |= Mesh::Flags::Water;
#endif

#if defined(SKYRIM)
		if (geometryType.all(RE::BSGeometry::Type::kDynamicTriShape))
			flags |= Mesh::Flags::Dynamic;
#elif defined(FALLOUT4)
		if (geometryType == 4)
			flags |= Mesh::Flags::Dynamic;
#endif

		float3x4 localToRoot;
		const bool isOrigin = pGeometry->world.translate == Util::Adapter::GetNiPoint3Zero();

		if (!isOrigin || isOrigin && isRootOrigin)
			localToRoot = Util::Math::ComputeLocalToRoot(rootWorldInverse, pGeometry->world);
		else {
			localToRoot = float3x4(
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f);
			flags |= Mesh::Flags::Origin;
		}

		if (auto* triShapeRD = geometryRuntimeData.rendererData) {  // Non-Skinned
			auto* pTriShape = netimmerse_cast<RE::BSTriShape*>(pGeometry);

#if defined(SKYRIM)
			const auto& triShapeRuntime = pTriShape->GetTrishapeRuntimeData();

			if (triShapeRuntime.vertexCount == 0) {
				logger::error("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Vertex count of 0 for {}", name ? name : "N/A");
				return CESEAdapter::RE::BSVisitControl::kContinue;
			}

			if (triShapeRuntime.triangleCount == 0) {
				logger::error("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Triangle count of 0 for {}", name ? name : "N/A");
				return CESEAdapter::RE::BSVisitControl::kContinue;
			}

			auto mesh = eastl::make_unique<Mesh>(baseFormType, Mesh::Type::Default, flags, name, pGeometry, localToRoot);

			mesh->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
#elif defined(FALLOUT4)
			if (pTriShape->numVertices == 0) {
				logger::error("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Vertex count of 0 for {}", name ? name : "N/A");
				return CESEAdapter::RE::BSVisitControl::kContinue;
			}

			if (pTriShape->numTriangles == 0) {
				logger::error("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Triangle count of 0 for {}", name ? name : "N/A");
				return CESEAdapter::RE::BSVisitControl::kContinue;
			}

			auto mesh = eastl::make_unique<Mesh>(baseFormType, Mesh::Type::Default, flags, name, pGeometry, localToRoot);

			mesh->BuildMesh(triShapeRD, pTriShape->numVertices, pTriShape->numTriangles, 0);
#endif
			mesh->BuildMaterial(geometryRuntimeData, form ? form->formID : 0);

			meshes.push_back(eastl::move(mesh));
		}
#if defined(SKYRIM)
		else if (auto* skinInstance = geometryRuntimeData.skinInstance) {  // Skinned
			auto& skinPartition = skinInstance->skinPartition;

			if (!skinPartition) {
				logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Invalid SkinPartition");
				return CESEAdapter::RE::BSVisitControl::kContinue;
			}

			if (skinPartition->vertexCount == 0) {
				logger::error("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Vertex count of 0 for {}", name ? name : "N/A");
				return CESEAdapter::RE::BSVisitControl::kContinue;
			}

			const auto skinNumPartitions = skinPartition->numPartitions;

			logger::debug("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Partitions: {}, VertexCount: {}, Unk24: [0x{:X}]", skinNumPartitions, skinPartition->vertexCount, skinPartition->unk24);

			// TODO: Proper partitioned mesh creation (read vertices only once, add only used vertices to each partitions mesh, etc...)
			for (size_t i = 0; i < skinNumPartitions; i++) {
				auto& partition = skinPartition->partitions[i];
	
				// Fix for modded geometry
				if (partition.triangles == 0) {
					logger::error("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Triangle count of 0 for {}", name ? name : "N/A");
					continue;
				}

				// Fix for modded geometry
				if (partition.bonesPerVertex > 0)
					flags |= Mesh::Flags::Skinned;

				auto mesh = eastl::make_unique<Mesh>(baseFormType, Mesh::Type::Default, flags, name, pGeometry, localToRoot, i);

				mesh->BuildMesh(partition.buffData, skinPartition->vertexCount, partition.triangles, partition.bonesPerVertex);
				mesh->BuildMaterial(geometryRuntimeData, form ? form->formID : 0);

				meshes.push_back(eastl::move(mesh));
			}
		}
#endif

		return CESEAdapter::RE::BSVisitControl::kContinue;
	});

	return meshes;
}

uint32_t SceneGraph::CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* pRoot)
{
	if (!pRoot)
		return 0;

	if (!path || strlen(path) == 0)
		return 0;

	auto formID = form ? form->GetFormID() : 0;

	Model* model = nullptr;
	{
		std::scoped_lock lock(m_ModelMutex);

		// We only need one buffer per model
		if (auto it = m_Models.find(path); it != m_Models.end())
			model = it->second.get();
	}

	if (model) {
		const bool nonInstanceable = model->GetMeshFlags().any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned) || model->GetMeshTypes().any(Mesh::Type::LandLOD);
		if (nonInstanceable) {
			auto modelCopy = model->Clone(pRoot, formID);

			if (modelCopy) {
				auto* modelCopyPtr = modelCopy.get();

				{
					std::scoped_lock lock(m_ModelMutex);
					m_Models.try_emplace(modelCopy->m_Name, eastl::move(modelCopy));
				}

				modelCopyPtr->CreateBuffers(this);
				modelCopyPtr->BuildBLAS();

				AddInstance(formID, pRoot, modelCopyPtr);
				return static_cast<uint32_t>(modelCopyPtr->m_Meshes.size());
			}
		}
		else {
			AddInstance(formID, pRoot, model);
			return static_cast<uint32_t>(model->m_Meshes.size());
		}
	}

	logger::debug("SceneGraph::CreateModelInternal - Path: {}, FormID [0x{:08X}], NiNode [0x{:08X}]: {}", path, formID, reinterpret_cast<uintptr_t>(pRoot), pRoot->name);

	// Creates all meshes, one for each valid BSGeometry found in the NiAVObject hierarchy
	auto meshes = CreateMeshes(pRoot, form);

	auto numMeshes = static_cast<uint32_t>(meshes.size());
	
	model = CommitModel(path, pRoot, form, meshes);

	if (model)
		AddInstance(form->GetFormID(), pRoot, model);

	return numMeshes;
}

Model* SceneGraph::CommitModel(const char* path, RE::NiAVObject* object, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes) {
	if (auto shapeCount = meshes.size(); shapeCount > 0) {

		auto type = form && form->formType.all(RE::FormType::ActorCharacter) ? Model::Type::Actor : Model::Type::Default;

		auto model = eastl::make_unique<Model>(path, type, object, form, meshes);
		auto* modelPtr = model.get();

		m_ModelMutex.lock();
		auto [it, emplaced] = m_Models.try_emplace(model->m_Name, eastl::move(model));
		m_ModelMutex.unlock();

		if (emplaced) {
			// Copy Command
			modelPtr->CreateBuffers(this);

			// Compute Command - Waits for copy
			modelPtr->BuildBLAS();

			// MSN Conversion - waits for copy
			if (modelPtr->ShouldQueueMSNConversion()) {
				auto graphicsCommandList = Renderer::GetSingleton()->GetGraphicsCommandList();
				graphicsCommandList->open();

				m_TextureManager->m_MSNConverter->Convert(modelPtr, graphicsCommandList, this);

				graphicsCommandList->close();

				auto* renderer = Renderer::GetSingleton();
				auto device = renderer->GetDevice();

				{
					std::scoped_lock lock(renderer->GetExecutionMutex());
					device->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Copy, modelPtr->m_SubmittedCopyInstance);
					device->executeCommandList(graphicsCommandList, nvrhi::CommandQueue::Graphics);
				}
			}

			logger::debug("SceneGraph::CommitModel - Commited {} TriShapes to [0x{:08X}]", shapeCount, reinterpret_cast<uintptr_t>(modelPtr));

			return modelPtr;
		}
		else {
			logger::warn("SceneGraph::CommitModel - {} failed emplace for {} TriShapes", model->m_Name, shapeCount);
		}
	}
	else {
		logger::debug("SceneGraph::CommitModel - No TriShapes to commit");
	}

	return nullptr;
}

Instance* SceneGraph::AddInstanceImpl(RE::NiAVObject* node, Model* model, RE::FormID formID)
{
	auto instance = eastl::make_unique<Instance>(formID, node, model);
	instance->model->AddRef();

	auto instancePtr = instance.get();

	m_Instances.Add(eastl::move(instance));

	return instancePtr;
}

void SceneGraph::AddInstance(RE::FormID formID, RE::NiAVObject* node, Model* model)
{
	if (auto* instance = AddInstanceImpl(node, model, formID))
		m_InstancesFormIDs[formID].push_back(instance);
}

void SceneGraph::AddInstance(RE::BGSObjectBlock* block, RE::NiAVObject* node, Model* model)
{
	auto* instance = AddInstanceImpl(node, model, 0);
	if (!instance)
		return;

	auto [it, inserted] = m_ObjectLODInstances.try_emplace(block, eastl::make_unique<ObjectLODBlockReference>(block));
	it->second->AddInstance(instance);
}

void SceneGraph::AddInstance(RE::BGSTerrainBlock* block, RE::NiAVObject* node, Model* model)
{
	auto* instance = AddInstanceImpl(node, model, 0);
	if (!instance)
		return;

	auto [it, inserted] = m_TerrainLODInstances.try_emplace(block, eastl::make_unique<TerrainLODBlockReference>(block));
	it->second->AddInstance(instance);
}

void SceneGraph::RunGarbageCollection()
{
	// Clear Models
	{
		std::scoped_lock modelLock(m_ModelReleaseMutex);

		for (auto it = m_ReleasedModels.begin(); it != m_ReleasedModels.end(); ) {
			auto* model = it->get();

			model->UpdateFlags();

			const bool release =
				(model->m_Flags & Model::Flags::BuffersUploaded) &&
				(model->m_Flags & Model::Flags::BLASBuilt);

			if (release)			
				it = m_ReleasedModels.erase(it);
			else
				++it;
		}		
	}
}
