#include "SceneGraph.h"

#include "Scene.h"

#include "Renderer.h"
#include "Util.h"
#include "ShaderUtils.h"

#include "Types/RE/RE.h"
#if defined(SKYRIM)
#include "Types/CommunityShaders/LightLimitFix.h"
#include "Types/CommunityShaders/ISLCommon.h"
#include "Types/WaterFlags.h"
#endif

#include "Pass/Raytracing/Common/Skinning.h"

#include "Core/SkinnedMesh.h"
#include "Core/DynamicMesh.h"
#include "Core/SubIndexMesh.h"
#include "Core/SubIndexSegmentMesh.h"

#include <chrono>

nvrhi::IBuffer* SceneGraph::GetLightBuffer() const { return m_LightBuffer.current(); }
nvrhi::IBuffer* SceneGraph::GetMeshBuffer() const { return m_MeshBuffer.current(); }
nvrhi::IBuffer* SceneGraph::GetInstanceBuffer() const { return m_InstanceBuffer.current(); }
nvrhi::IBuffer* SceneGraph::GetTransformBuffer() const { return m_TransformManager->GetBuffer(); }

void SceneGraph::Initialize()
{
	const auto maxThreads = std::thread::hardware_concurrency() - 1u;
	const auto numWorkerThreads = std::min(maxThreads, Scene::GetSingleton()->m_Settings.AdvancedSettings.NumWorkerThreads);

	m_ThreadPool = eastl::make_unique<ThreadPool>(numWorkerThreads);

	auto device = Renderer::GetSingleton()->GetDevice();

	m_MeshBuffer = Util::CreateStructuredRingBuffer<MeshData>(device, Constants::NUM_MESHES_MAX, "Mesh Buffer");
	m_InstanceBuffer = Util::CreateStructuredRingBuffer<InstanceData>(device, Constants::NUM_INSTANCES_MAX, "Instance Buffer");
	m_LightBuffer = Util::CreateStructuredRingBuffer<LightData>(device, Constants::LIGHTS_MAX, "Light Buffer");

	m_TransformManager = eastl::make_unique<TransformManager>();

	m_MaterialManager = eastl::make_shared<MaterialManager>();

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
	const auto updateStart = std::chrono::high_resolution_clock::now();
	auto phaseStart = updateStart;

	UpdateLights(commandList);

	const auto timings = Scene::GetSingleton()->m_Settings.DebugSettings.Timings;
	if (timings) {
		m_UpdateTimings.clear();

		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::UpdateLights", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	eastl::vector<RE::BSTriShape*> destroyedMeshes; 
	{
		std::scoped_lock lock(m_MeshDestroyMutex);
		destroyedMeshes = eastl::move(m_DestroyedMeshes);
	}

	const uint64_t fence = Renderer::GetSingleton()->GetLastSubmittedFence();

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
		}

		m_PendingMeshDestroy.push_back({ eastl::move(it->second), fence });
		m_DirectMeshes.erase(it);
	}

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::DestroyMeshes", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	m_NumMeshes = 0;
	m_NumInstances = 0;

	m_CurrentVisible.clear();
	m_CurrentVisible.reserve(m_DirectMeshes.size());

	m_UpdateList.clear();
	m_UpdateList.reserve(m_DirectMeshes.size());

	m_CreateList.clear();
	m_CreateCandidates.clear();

	const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

	// Phase A: Fast traversal — collect into update/create lists, skip heavy processing
	{
		auto worldRootNode = RE::Main::GetSingleton()->WorldRootNode();
		Util::Traversal::ScenegraphTriShapes(worldRootNode, [this, frameIndex](RE::BSTriShape* bsTriShape, RE::TESObjectREFR* refr) -> CESEAdapter::RE::BSVisitControl {
			auto it = m_DirectMeshes.find(bsTriShape);
			if (it != m_DirectMeshes.end()) {
				auto mesh = it->second.get();
				m_UpdateList.push_back({ mesh, refr });
				m_CurrentVisible.push_back(mesh);
			} else {
				m_CreateList.push_back({ bsTriShape, refr });
			}
			return CESEAdapter::RE::BSVisitControl::kContinue;
		});
	}

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseA-Traversal", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase B + C1 (parallel): Update known meshes AND filter new meshes via thread pool
	{
		const size_t numWorkers = std::max<size_t>(1, m_ThreadPool->GetThreadCount());
		const size_t totalWork = m_UpdateList.size();
		const size_t totalCreate = m_CreateList.size();

		auto doUpdate = [&](auto& entry) {
			auto& [mesh, refr] = entry;
			mesh->SetLastVisitedFrame(frameIndex);

			if (!mesh->AsSubIndexMesh()) {	
				const bool ownerChanged = mesh->SetOwner(refr);
				auto cluster = mesh->GetCluster();

				if (ownerChanged || !cluster) {
					if (cluster) {
						cluster->RemoveMember(mesh);
						MarkClusterDirty(cluster);
					}

					cluster = GetOrCreateCluster(refr, mesh->GetTriShape());
					cluster->AddMember(mesh);
					MarkClusterDirty(cluster);
				}
			}

			mesh->SetHidden(false);
			mesh->Update(commandList);
		};

		auto doFilter = [&](size_t start, size_t end, eastl::vector<MeshCreateCandidate>& out) {
			for (size_t i = start; i < end; ++i) {
				auto& [bsTriShape, refr] = m_CreateList[i];

				if (bsTriShape->GetType().none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kDynamicTriShape, RE::BSGeometry::Type::kSubIndexTriShape))
					continue;

				const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);
				auto* shaderProperty = geometryData.shaderProperty;
				if (!shaderProperty)
					continue;

				const auto materialType = shaderProperty->GetMaterialType();
				const bool isLightingShader = (materialType == RE::BSShaderMaterial::Type::kLighting);
				const bool isEffectShader = (materialType == RE::BSShaderMaterial::Type::kEffect);
				const bool isWaterShader = (materialType == RE::BSShaderMaterial::Type::kWater);

				auto* alphaProperty = geometryData.alphaProperty;
				const bool isAlphaBlend = alphaProperty ? alphaProperty->GetAlphaBlending() : false;
				const bool validEffect = isEffectShader && !isAlphaBlend;

				// Exclude procedural and displacement water
				if (isWaterShader) {
					auto waterShaderProperty = reinterpret_cast<RE::BSWaterShaderProperty*>(shaderProperty);
					const auto waterFlags = waterShaderProperty->waterFlags.underlying();

					if (waterFlags & WaterFlags::kProcedural || waterFlags & WaterFlags::kDisplacement)
						continue;
				}

				if (!isLightingShader && !validEffect && !isWaterShader)
					continue;

				// Exclude tree lod and grass for now
				const auto shaderPropertyRTTI = shaderProperty->GetRTTI();
				if (shaderPropertyRTTI == Constants::rtti::BSDistantTreeShaderProperty.get() ||
				    shaderPropertyRTTI == Constants::rtti::BSGrassShaderProperty.get())
					continue;

				if (Util::Geometry::IsBlocklisted(bsTriShape->name.c_str()))
					continue;

				const bool skinned = !geometryData.rendererData && geometryData.skinInstance && geometryData.skinInstance->skinPartition && geometryData.skinInstance->skinPartition->numPartitions > 0;
				if (!skinned) {
					const auto& trishapeData = bsTriShape->GetTrishapeRuntimeData();
					if (trishapeData.vertexCount == 0 || trishapeData.triangleCount == 0)
						continue;
				}

				const auto rendererData = skinned ? geometryData.skinInstance->skinPartition->partitions[0].buffData : geometryData.rendererData;
				if (!rendererData)
					continue;

				out.push_back({ bsTriShape, refr });
			}
		};

		m_PerWorkerCreateCandidates.resize(numWorkers);
		for (auto& candidates : m_PerWorkerCreateCandidates)
			candidates.clear();

		bool anyDispatched = false;

		if (totalWork > 0) {
			const size_t chunkSize = (totalWork + numWorkers - 1) / numWorkers;

			for (size_t start = 0; start < totalWork; start += chunkSize) {
				size_t end = std::min(start + chunkSize, totalWork);

				m_ThreadPool->Enqueue([&, start, end]() {
					for (size_t i = start; i < end; ++i)
						doUpdate(m_UpdateList[i]);
				});
			}

			anyDispatched = true;
		}

		if (totalCreate > 0) {
			const size_t chunkSize = (totalCreate + numWorkers - 1) / numWorkers;

			for (size_t start = 0; start < totalCreate; start += chunkSize) {
				size_t end = std::min(start + chunkSize, totalCreate);
				const size_t idx = start / chunkSize;

				m_ThreadPool->Enqueue([&, start, end, idx]() {
					doFilter(start, end, m_PerWorkerCreateCandidates[idx]);
				});
			}

			anyDispatched = true;
		}

		if (anyDispatched)
			m_ThreadPool->WaitAll();

		m_CreateCandidates.reserve(m_CreateList.size());
		for (auto& wc : m_PerWorkerCreateCandidates)
			m_CreateCandidates.insert(m_CreateCandidates.end(), wc.begin(), wc.end());
	}

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseB-Parallel", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase C2 (serial): GPU resource creation for validated candidates
	for (auto& [bsTriShape, refr] : m_CreateCandidates) {
		if (auto created = BaseMesh::Create(bsTriShape, commandList)) {
			created->SetOwner(refr);
			auto [it2, inserted] = m_DirectMeshes.emplace(bsTriShape, eastl::move(created));
			if (inserted) {
				auto mesh = it2->second.get();

				// SubIndexMesh: not a member of any cluster itself; the K SubIndexSegmentMesh
				// children will be added to their own clusters by SubIndexMesh::Update.
				if (!mesh->AsSubIndexMesh()) {
					auto* cluster = GetOrCreateCluster(refr, bsTriShape);
					cluster->AddMember(mesh);
					MarkClusterDirty(cluster);
				}

				mesh->SetLastVisitedFrame(frameIndex);
				mesh->Update(commandList);
				m_CurrentVisible.push_back(mesh);
			}
		}
	}

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseC2-Create", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase D: Hide meshes whose trishapes were not visited by the traversal this frame
	for (auto& mesh : m_PreviousVisible) {
		if (mesh->GetLastVisitedFrame() != frameIndex) {
			mesh->SetHidden(true);

			if (auto* cluster = mesh->GetCluster()) {
				cluster->RemoveMember(mesh);
				MarkClusterDirty(cluster);
			}
		}
	}

	m_PreviousVisible.swap(m_CurrentVisible);

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseD-Hide", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase E: Material flush
	m_MaterialManager->Flush(commandList);

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseE-MaterialFlush", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase F: Drop clusters whose meshes were all destroyed this frame.
	auto removeEmptyClusters = [this](auto& clusters) {
		for (auto it = clusters.begin(); it != clusters.end(); ) {
			if (it->second->Empty()) {
				m_DirtyClusters.erase(it->second.get());
				it = clusters.erase(it);
			} else {
				++it;
			}
		}
	};
	removeEmptyClusters(m_OwnerClusters);
	removeEmptyClusters(m_OrphanClusters);
	removeEmptyClusters(m_SubIndexSegmentClusters);

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseF-DropClusters", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase G (parallel): each cluster atomically reserves its own offsets in MeshData / InstanceData.
	{
		m_AllClusters.clear();
		m_AllClusters.reserve(
			m_OwnerClusters.size() +
			m_OrphanClusters.size() +
			m_SubIndexSegmentClusters.size());

		for (auto& [_, cluster] : m_OwnerClusters)
			m_AllClusters.push_back(cluster.get());

		for (auto& [_, cluster] : m_OrphanClusters)
			m_AllClusters.push_back(cluster.get());

		for (auto& [_, cluster] : m_SubIndexSegmentClusters)
			m_AllClusters.push_back(cluster.get());

		m_NumMeshes = 0;
		m_NumInstances = 0;
		bool reportedMeshLimit = false;
		bool reportedInstanceLimit = false;

		const size_t numWorkers = std::max<size_t>(1, m_ThreadPool->GetThreadCount());
		const size_t totalWork = m_AllClusters.size();

		if (totalWork > 0) {
			const size_t chunkSize = (totalWork + numWorkers - 1) / numWorkers;

			for (size_t start = 0; start < totalWork; start += chunkSize) {
				size_t end = std::min(start + chunkSize, totalWork);

				m_ThreadPool->Enqueue([&, start, end]() {
					for (size_t i = start; i < end; ++i) {
						auto& cluster = m_AllClusters[i];

						const auto& meshData = cluster->Update();
						const uint32_t meshCount = static_cast<uint32_t>(meshData.size());

						if (meshCount == 0)
							continue;

						// Acdquire indices and advance counts atomically
						uint32_t firstMesh = 0;
						uint32_t instanceIndex = 0;
						{
							std::scoped_lock mutex(m_BLASClusterUpdateMutex);

							if (m_NumMeshes + meshCount > Constants::NUM_MESHES_MAX) {
								cluster->SetValid(false);
								if (!reportedMeshLimit) {
									logger::critical("SceneGraph::Update - Mesh capacity ({}) reached; omitting a cluster with {} mesh entries.", Constants::NUM_MESHES_MAX, meshCount);
									reportedMeshLimit = true;
								}
								continue;
							}

							if (m_NumInstances + 1 > Constants::NUM_INSTANCES_MAX) {
								cluster->SetValid(false);
								if (!reportedInstanceLimit) {
									logger::critical("SceneGraph::Update - Instance capacity ({}) reached; omitting a cluster with {} mesh entries.", Constants::NUM_INSTANCES_MAX, meshCount);
									reportedInstanceLimit = true;
								}
								continue;
							}

							firstMesh = m_NumMeshes;
							m_NumMeshes += meshCount;

							instanceIndex = m_NumInstances++;
						}

						// Patch InstanceID into the cluster's mesh data before copying
						for (auto& md : cluster->m_MeshData)
							md.InstanceID = static_cast<uint16_t>(instanceIndex);

						// Update Mesh Data
						std::memcpy(m_MeshData.data() + firstMesh, meshData.data(), meshCount * sizeof(MeshData));

						// Set Instance Index
						cluster->SetInstanceIndex(instanceIndex);

						// Update Instance Data
						cluster->WriteInstanceData(firstMesh, meshCount, m_InstanceData[instanceIndex]);
					}
				});
			}

			m_ThreadPool->WaitAll();
		}
	}

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseG-ClusterUpdate", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	if (m_NumMeshes > 0)
		commandList->writeBuffer(GetMeshBuffer(), m_MeshData.data(), m_NumMeshes * sizeof(MeshData));

	if (m_NumInstances > 0)
		commandList->writeBuffer(GetInstanceBuffer(), m_InstanceData.data(), m_NumInstances * sizeof(InstanceData));

	m_TransformManager->Flush(commandList);

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::BufferWrites", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});

		const auto totalEnd = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::Total", 0.0f, std::chrono::duration<float, std::milli>(totalEnd - updateStart).count()});
	}
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

template <typename Key, typename Map>
BLASCluster* SceneGraph::GetOrCreateClusterImpl(Map& a_map, std::shared_mutex& a_mutex, Key a_key, RE::TESObjectREFR* a_owner)
{
	{
		std::shared_lock lock(a_mutex);
		auto it = a_map.find(a_key);
		if (it != a_map.end())
			return it->second.get();
	}

	BLASCluster* result = nullptr;
	bool didInsert = false;
	{
		std::unique_lock lock(a_mutex);
		auto [it, inserted] = a_map.try_emplace(a_key, nullptr);
		if (inserted)
			it->second = eastl::make_unique<BLASCluster>(a_owner);
		result = it->second.get();
		didInsert = inserted;
	} // exclusive lock released here

	if (didInsert)
		MarkClusterDirty(result); // separate mutex, safe outside the cluster lock

	return result;
}

BLASCluster* SceneGraph::GetOrCreateCluster(RE::TESObjectREFR* owner, RE::BSTriShape* bsTriShape)
{
	return owner
		? GetOrCreateClusterImpl(m_OwnerClusters, m_OwnerClusterMutex, owner, owner)
		: GetOrCreateClusterImpl(m_OrphanClusters, m_OrphanClusterMutex, bsTriShape, nullptr);
}

BLASCluster* SceneGraph::GetOrCreateSegmentCluster(SubIndexSegmentMesh* segment, RE::TESObjectREFR* owner)
{
	{
		std::shared_lock lock(m_SegmentClusterMutex);

		auto it = m_SubIndexSegmentClusters.find(segment);
		if (it != m_SubIndexSegmentClusters.end())
			return it->second.get();
	}

	BLASCluster* result = nullptr;
	{
		std::unique_lock lock(m_SegmentClusterMutex);

		auto [it, inserted] = m_SubIndexSegmentClusters.try_emplace(segment, nullptr);
		if (inserted)
			it->second = eastl::make_unique<BLASCluster>(owner);

		result = it->second.get();
	}

	return result;
}

void SceneGraph::MarkClusterDirty(BLASCluster* cluster)
{
	if (!cluster) 
		return;

	std::scoped_lock lock(m_ClusterDirtyMutex);
	m_DirtyClusters.emplace(cluster);
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

uint32_t SceneGraph::AllocateTransformIndex()
{
	return m_TransformManager->AllocateTransformIndex();
}

void SceneGraph::WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform)
{
	m_TransformManager->WriteTransformData(index, transform, prevTransform);
}
