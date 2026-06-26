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

	m_MaterialManager = eastl::make_unique<MaterialManager>();

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

		auto* alphaProperty = geometryData.alphaProperty;
		const bool isAlphaBlend = alphaProperty ? alphaProperty->GetAlphaBlending() : false;

		const bool validEffect = isEffectShader && !isAlphaBlend;

		if (!isLightingShader && !validEffect && !isWaterShader && !isTreeLODShader && !isGrassShader)
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
					// CPU-side change detection while the trishape + skin instance are alive.
					// SkinnedMesh::Update recomputes bone matrices and queues the GPU skinning pass.
					mesh->Update();

					// Capture transforms while the owner/trishape are alive; cluster consumes cached values later.
					UpdateMeshTransforms(mesh.get(), clusterOwner, bsTriShape);
				}
			}
			else if (!hidden) {
				if (auto created = BaseMesh::Create(bsTriShape, commandList)) {
					created->SetOwner(clusterOwner);

					mesh = nullptr;
					{
						std::scoped_lock lock(m_MeshMutex);
						auto [it, inserted] = m_DirectMeshes.emplace(bsTriShape, created);
						if (inserted)
							mesh = it->second;
					}

					if (mesh) {
						mesh->Update();

						GetOrCreateCluster(clusterOwner, bsTriShape)->AddMember(mesh);
						UpdateMeshTransforms(mesh.get(), clusterOwner, bsTriShape);
					}
				}
			}
		}

		return CESEAdapter::RE::BSVisitControl::kContinue;
	});

	// Upload pending material data to material buffer
	m_MaterialManager->Flush(commandList);

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

void SceneGraph::ReleaseTexture(RE::BSGraphics::Texture* texture)
{
	m_TextureManager->ReleaseTexture(texture);
}