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

#include <chrono>

nvrhi::IBuffer* SceneGraph::GetLightBuffer() const { return m_LightBuffer.current(); }
nvrhi::IBuffer* SceneGraph::GetMeshBuffer() const { return m_MeshBuffer.current(); }
nvrhi::IBuffer* SceneGraph::GetInstanceBuffer() const { return m_InstanceBuffer.current(); }

void SceneGraph::Initialize()
{
	auto device = Renderer::GetSingleton()->GetDevice();

	m_MeshBuffer = Util::CreateStructuredRingBuffer<MeshData>(device, Constants::NUM_MESHES_MAX, "Mesh Buffer");
	m_InstanceBuffer = Util::CreateStructuredRingBuffer<InstanceData>(device, Constants::NUM_INSTANCES_MAX, "Instance Buffer");
	m_LightBuffer = Util::CreateStructuredRingBuffer<LightData>(device, Constants::LIGHTS_MAX, "Light Buffer");

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

		m_DynamicVertexReadDescriptors = eastl::make_unique<BindlessTableManager>(device, bindlessLayoutDesc, true);
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

		m_DynamicVertexDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	// Dynamic vertex live SRV descriptor table (skinned dynamic float4 positions; SRV read by RT shading)
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8).setSize(UINT_MAX)
		};

		m_DynamicVertexLiveDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
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

	commandList->writeBuffer(GetLightBuffer(), m_LightData.data(), numLights * sizeof(LightData));
#endif
}

void SceneGraph::UpdateLODVisibility()
{
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
	auto it = m_DirectMeshes.find(bsTriShape);
	if (it == m_DirectMeshes.end())
		return;

	it->second->OnDestroy();

	{
		std::scoped_lock lock(m_MeshDestroyMutex);
		m_DestroyedMeshes.push_back(bsTriShape);
	}
}

void SceneGraph::UpdateDynamicData(RE::BSDynamicTriShape* bsDynamicTriShape)
{
	auto it = m_DirectMeshes.find(bsDynamicTriShape);
	if (it == m_DirectMeshes.end())
		return;

	if (auto dynamicMesh = it->second->AsDynamicMesh()) {
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

	// Defer release until the owning slot's GPU work completes; ProcessPendingMeshDestroys
	// is called from StartExecution() after the per-slot fence resolves.
	for (auto destroyedMesh: destroyedMeshes)
	{
		auto it = m_DirectMeshes.find(destroyedMesh);
		if (it == m_DirectMeshes.end())
			continue;

		auto* mesh = it->second.get();

		if (auto* cluster = mesh->GetCluster()) {
			cluster->RemoveMember(mesh);
			MarkClusterDirty(cluster);
			mesh->SetCluster(nullptr);
		}

		uint64_t fence = Renderer::GetSingleton()->GetLastSubmittedFence();
		m_PendingMeshDestroy.push_back({ it->second, fence });
		m_DirectMeshes.erase(it);
	}

	auto shadowSceneNode = Util::Adapter::GetShaderManagerState().shadowSceneNode[0];

	// Hardcoded for now
	const bool skipClustering = false;

	m_NumMeshes = 0;
	m_NumInstances = 0;
	
	m_CurrentVisible.clear();
	m_CurrentVisible.reserve(m_DirectMeshes.size());

	const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

	Util::Traversal::ScenegraphTriShapes(shadowSceneNode, [&](RE::BSTriShape* bsTriShape, bool hidden, RE::TESObjectREFR* ownerRefr) -> CESEAdapter::RE::BSVisitControl {

		if (bsTriShape->GetType().none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kDynamicTriShape))
			return CESEAdapter::RE::BSVisitControl::kContinue;

		const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);

		auto* shaderProperty = geometryData.shaderProperty;
		if (!shaderProperty) 
			return CESEAdapter::RE::BSVisitControl::kContinue;

		const auto materialType = shaderProperty->GetMaterialType();

		const bool isLightingShader = (materialType == RE::BSShaderMaterial::Type::kLighting);
		const bool isEffectShader = (materialType == RE::BSShaderMaterial::Type::kEffect);
		const bool isWaterShader = (materialType == RE::BSShaderMaterial::Type::kWater);

		const auto shaderPropertyRTTI = shaderProperty->GetRTTI();
		const bool isTreeLODShader = (shaderPropertyRTTI == Constants::rtti::BSDistantTreeShaderProperty.get());
		const bool isGrassShader = (shaderPropertyRTTI == Constants::rtti::BSGrassShaderProperty.get());

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

		// When clustering is skipped, force a null owner so every mesh lands in its own orphan cluster.
		RE::TESObjectREFR* const clusterOwner = skipClustering ? nullptr : ownerRefr;

		auto it = m_DirectMeshes.find(bsTriShape);
		if (it != m_DirectMeshes.end()) {
			auto& mesh = it->second;

			// If exists - update owner/visibility/data state (dirty flags live inside the mesh), else - create if visible 
			mesh->SetLastVisitedFrame(frameIndex);

			// SetOwner returns true if the owner changed
			const bool ownerChanged = mesh->SetOwner(clusterOwner);

			// Get current cluster
			auto cluster = mesh->GetCluster();

			if (ownerChanged || !cluster) {
				// Remove from old cluster if it existed
				if (cluster) {
					cluster->RemoveMember(mesh.get());
					MarkClusterDirty(cluster);
				}

				cluster = GetOrCreateCluster(clusterOwner, bsTriShape);
				cluster->AddMember(mesh);
				MarkClusterDirty(cluster);
			}

			mesh->SetHidden(hidden);

			if (!hidden) {
				mesh->Update();
				m_CurrentVisible.push_back(mesh.get());
			}
		}
		else if (!hidden) {
			if (auto created = BaseMesh::Create(bsTriShape, commandList)) {
				created->SetOwner(clusterOwner);
	
				auto [it2, inserted] = m_DirectMeshes.emplace(bsTriShape, created);
				if (inserted) {
					auto& mesh = it2->second;
	
					auto* cluster = GetOrCreateCluster(clusterOwner, bsTriShape);
					cluster->AddMember(mesh);
					MarkClusterDirty(cluster);

					mesh->SetLastVisitedFrame(frameIndex);
	
					mesh->Update();
					m_CurrentVisible.push_back(mesh.get());
				}
			}
		}

		return CESEAdapter::RE::BSVisitControl::kContinue;
	});

	// Hide meshes whose trishapes were not visited by the traversal this frame
	{
		for (auto& mesh : m_PreviousVisible) {
			if (mesh->GetLastVisitedFrame() != frameIndex) {
				mesh->SetHidden(true);
				if (auto* cluster = mesh->GetCluster()) {
					cluster->RemoveMember(mesh);
					MarkClusterDirty(cluster);
					mesh->SetCluster(nullptr);
				}
			}
		}
		m_PreviousVisible = eastl::move(m_CurrentVisible);
	}

	// Upload pending material data to material buffer
	m_MaterialManager->Flush(commandList);

	// Drop clusters whose meshes were all destroyed this frame.
	for (auto it = m_OwnerClusters.begin(); it != m_OwnerClusters.end(); ) {
		if (it->second->Empty()) {
			eastl::erase(m_DirtyClusters, it->second.get());
			it = m_OwnerClusters.erase(it);
		}
		else
			++it;
	}

	for (auto it = m_OrphanClusters.begin(); it != m_OrphanClusters.end(); ) {
		if (it->second->Empty()) {
			eastl::erase(m_DirtyClusters, it->second.get());
			it = m_OrphanClusters.erase(it);
		}
		else
			++it;
	}

	// Populate per-instance + per-geometry data for shader-side geometry lookup, in the same
	// owner-then-orphan / member order the TLAS and BLAS use (so InstanceID()/GeometryIndex() align).
	auto appendInstance = [&](BLASCluster* cluster) {
		if (m_NumInstances >= Constants::NUM_INSTANCES_MAX)
			return;

		InstanceData instance;
		if (cluster->GetData(m_MeshData.data(), m_NumMeshes, instance, m_Lights, m_LightData)) {
			cluster->SetInstanceIndex(m_NumInstances);
			m_InstanceData[m_NumInstances] = instance;
			m_NumInstances++;
		}
	};

	for (auto& [owner, cluster] : m_OwnerClusters)
		appendInstance(cluster.get());

	for (auto& [bsTriShape, cluster] : m_OrphanClusters)
		appendInstance(cluster.get());

	if (m_NumMeshes >= Constants::NUM_MESHES_MAX)
		logger::critical("SceneGraph::Update - Number of meshes of {} exceeds the maximum of {}", m_NumMeshes, Constants::NUM_MESHES_MAX);

	if (m_NumMeshes > 0)
		commandList->writeBuffer(GetMeshBuffer(), m_MeshData.data(), m_NumMeshes * sizeof(MeshData));

	if (m_NumInstances >= Constants::NUM_INSTANCES_MAX)
		logger::critical("SceneGraph::Update - Number of instances of {} exceeds the maximum of {}", m_NumInstances, Constants::NUM_INSTANCES_MAX);

	if (m_NumInstances > 0)
		commandList->writeBuffer(GetInstanceBuffer(), m_InstanceData.data(), m_NumInstances * sizeof(InstanceData));
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
		if (!slot) {
			slot = eastl::make_unique<BLASCluster>(owner);
			MarkClusterDirty(slot.get());
		}
		return slot.get();
	}
	
	auto& slot = m_OrphanClusters[bsTriShape];
	if (!slot) {
		slot = eastl::make_unique<BLASCluster>(nullptr);
		MarkClusterDirty(slot.get());
	}
	return slot.get();

}

void SceneGraph::MarkClusterDirty(BLASCluster* cluster)
{
	if (!cluster) return;
	
	if (!cluster->m_IsDirty) {
		cluster->MarkDirty();
		m_DirtyClusters.push_back(cluster);
	}
}

void SceneGraph::BuildClusters(nvrhi::ICommandList* commandList)
{
	// Process only clusters that were marked dirty.
	for (auto* cluster : m_DirtyClusters)
		cluster->BuildUpdate(commandList, this);
	
	m_DirtyClusters.clear();
}


void SceneGraph::ReleaseTexture(RE::BSGraphics::Texture* texture)
{
	m_TextureManager->ReleaseTexture(texture);
}

void SceneGraph::ProcessPendingMeshDestroys(uint64_t completedFence)
{
	m_PendingMeshDestroy.erase(
		eastl::remove_if(m_PendingMeshDestroy.begin(), m_PendingMeshDestroy.end(),
			[completedFence](const PendingDestroy& p) { return p.fenceValue <= completedFence; }),
		m_PendingMeshDestroy.end());
}