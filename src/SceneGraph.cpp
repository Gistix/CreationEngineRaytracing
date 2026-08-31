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

#include "Core/Mesh/SkinnedMesh.h"
#include "Core/Mesh/DynamicMesh.h"
#include "Core/Mesh/SubIndexMesh.h"
#include "Core/Mesh/SubIndexSegmentMesh.h"
#include "Core/ParallelTriShapeWalker.h"

#include <chrono>
#include <numbers>

void SceneGraph::Initialize()
{
	const auto maxThreads = std::thread::hardware_concurrency() - 1u;
	const auto numWorkerThreads = std::min(maxThreads, Scene::GetSingleton()->m_Settings.AdvancedSettings.NumWorkerThreads);

	m_ThreadPool = eastl::make_unique<ThreadPool>(numWorkerThreads);

	auto device = Renderer::GetSingleton()->GetDevice();

	// Mesh slot remap buffer: one uint2 per mesh (ByteAddress), ring-buffered
	{
		auto remapDesc = nvrhi::BufferDesc()
			.setByteSize(Constants::NUM_MESHES_MAX * 4)
			.setCanHaveRawViews(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
			.setDebugName("Mesh Slot Remap Buffer");
		m_MeshSlotRemapBuffer = RingBuffer(device, remapDesc, "Mesh Slot Remap Buffer");
	}


	m_InstanceBuffer = Util::CreateStructuredRingBuffer<InstanceData>(device, Constants::NUM_INSTANCES_MAX, "Instance Buffer");
	m_LightBuffer = Util::CreateStructuredRingBuffer<LightData>(device, Constants::LIGHTS_MAX, "Light Buffer");

	m_MeshManager = eastl::make_unique<MeshManager>();

	m_MaterialManager = eastl::make_shared<MaterialManager>();

	// Triangle bindless descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::RawBuffer_SRV(1).setSize(UINT_MAX)
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
	auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
	auto* playerCamera = RE::PlayerCamera::GetSingleton();

	// Mimics the logic of Main::Draw (kind of)
	m_DrawFirstPerson = Util::Adapter::IsInFirstPerson(playerCharacter, playerCamera)
		&& Scene::GetSingleton()->GetMenuState().none(MenuState::MainMenu);

	// Might not be exactly how the game does it, but the first person node is always at origin, except during first person view rendering
	m_FirstPersonPosition = m_DrawFirstPerson ? Util::Adapter::GetCameraEyePosition() - Util::Adapter::GetFirstPersonNodePosition(playerCamera) : Util::Adapter::GetZeroNiPoint3();


	const auto* tesCamera = playerCamera->currentState->camera;
	m_Camera = tesCamera ? Util::Game::FindNiCamera(tesCamera->cameraRoot.get()) : nullptr;
}

void SceneGraph::UpdateLights(nvrhi::ICommandList* commandList)
{
	auto* shadowSceneNode = Util::Adapter::GetShadowSceneNode(0);

#if defined(SKYRIM)
	auto& mainSSNRuntimeData = shadowSceneNode->GetRuntimeData();
	auto& activeLights = mainSSNRuntimeData.activeLights;
	auto& activeShadowLights = mainSSNRuntimeData.activeShadowLights;
#elif defined(FALLOUT4)
	auto& activeLights = shadowSceneNode->activeLights;
	auto& activeShadowLights = shadowSceneNode->activeShadowLights;
#endif

	// Update Light Vector
	{
		m_TempActiveLights.clear();
		m_TempActiveLights.reserve(activeLights.size() + activeShadowLights.size());

		auto collectLights = [&](const auto& lights) {
			for (const auto& activeLight : lights)
			{
				auto* ptr = activeLight.get();
				if (!ptr)
					continue;

				m_TempActiveLights.insert(ptr);
				m_Lights.try_emplace(ptr, ptr);
			}
		};

		collectLights(activeLights);
		collectLights(activeShadowLights);

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
		if (!niLight)
			continue;

		bool isSpotLight = false;
		RE::TESObjectLIGH* ligh = nullptr;

		const auto refr = Util::Adapter::GetUserData(niLight);
		if (refr) {
			if (refr->IsDisabled())
				light.m_Active = false;

			if (auto* objRef = refr->GetObjectReference()) {
				if (objRef->GetFormType() == CESEAdapter::RE::FormType::Light) {
					ligh = objRef->As<RE::TESObjectLIGH>();

					if (ligh)
						isSpotLight = Util::Adapter::IsSpotLight(ligh);
				}
			}
		}


		if (Util::Adapter::IsNiAVObjectHidden(niLight))
			light.m_Active = false;

		if (bsLight->IsShadowLight())
		{
			auto* shadowLight = reinterpret_cast<RE::BSShadowLight*>(bsLight);

			if (shadowLight->GetRuntimeData().maskIndex == 255)
				light.m_Active = false;
		}

		auto runtimeData = Util::Adapter::GetLightRuntimeData(niLight);

#if defined(SKYRIM)
		auto flags = std::bit_cast<LightLimitFix::LightFlags>(runtimeData.ambient.red);

		if (flags & LightLimitFix::LightFlags::Disabled)
			light.m_Active = false;
#endif

		// Update Light Data
		{
			auto& lightData = m_LightData[numLights];

			lightData.Color = Util::Math::Float3(runtimeData.diffuse);

			lightData.Radius = runtimeData.radius;

			if ((lightData.Color.x + lightData.Color.y + lightData.Color.z) <= 1e-4 || lightData.Radius <= 1e-4)
				light.m_Active = false;

			// Clear instances
			light.m_Instances.clear();

			if (light.m_Active)
				light.UpdateInstances();

			lightData.Position = Util::Math::Float3(niLight->world.translate);

			lightData.InvRadius = 1.0f / runtimeData.radius;

			lightData.Fade = runtimeData.fade;

			if (lightingSettings.LodDimmer)
				lightData.Fade *= bsLight->lodDimmer;

			if (isSpotLight) {
				lightData.Type = LightType::Spot;
				lightData.Direction = Util::Math::Normalize(Util::Math::GetMatrixColumn(niLight->world.rotate, 0));
				lightData.CosOuterAngle = std::cosf(ligh->data.fov * std::numbers::pi_v<float> / 180.0f);
				lightData.CosInnerAngle = 1.0f;
			} else {
				lightData.Type = LightType::Point;
				lightData.Direction = float3(0.0f, 0.0f, 0.0f);
				lightData.CosOuterAngle = -1.0f;
				lightData.CosInnerAngle = -1.0f;
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
}

void SceneGraph::OnDestroy(RE::BSTriShape* bsTriShape)
{
	auto it = m_Meshes.find(bsTriShape);
	if (it == m_Meshes.end())
		return;

	it->second->OnDestroy();

	{
		std::scoped_lock lock(m_MeshDestroyMutex);
		m_DestroyedMeshes.push_back(bsTriShape);
	}
}

void SceneGraph::UpdateDynamicData(RE::BSDynamicTriShape* bsDynamicTriShape)
{
	auto it = m_Meshes.find(reinterpret_cast<RE::BSTriShape*>(bsDynamicTriShape));
	if (it == m_Meshes.end())
		return;

	if (auto dynamicMesh = it->second->AsDynamicMesh()) {
		// Function is called through a hook thats already between lock
		// Acessing without locking here is safe and correct
		Util::Adapter::UpdateDynamicData(dynamicMesh, bsDynamicTriShape);
	}
}

void SceneGraph::Update(nvrhi::ICommandList* commandList)
{
	const auto updateStart = std::chrono::high_resolution_clock::now();
	auto phaseStart = updateStart;

	UpdateLights(commandList);

	const auto timings = Scene::GetSingleton()->m_Settings.DebugSettings.Timings == TimingMode::Extended;
	if (timings) {
		m_UpdateTimings.clear();

		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::UpdateLights", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	{
		std::scoped_lock lock(m_MeshDestroyMutex);
		m_DestroyedMeshesSwap.swap(m_DestroyedMeshes);
	}

	const uint64_t fence = Renderer::GetSingleton()->GetLastSubmittedFence();

	// Defer release until the owning slot's GPU work completes; ProcessPendingMeshDestroys
	// is called from StartExecution() after the per-slot fence resolves.
	for (auto destroyedMesh: m_DestroyedMeshesSwap)
	{
		auto it = m_Meshes.find(destroyedMesh);
		if (it == m_Meshes.end())
			continue;

		auto* mesh = it->second.get();

		if (auto* cluster = mesh->GetCluster()) {
			cluster->RemoveMember(mesh);
			MarkClusterDirty(cluster);
		}

		m_PendingMeshDestroy.push_back({ eastl::move(it->second), fence });
		m_Meshes.erase(it);
	}

	m_DestroyedMeshesSwap.clear();

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::DestroyMeshes", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	m_NumMeshes = 0;
	m_NumInstances = 0;

	m_CurrentVisible.clear();
	m_CurrentVisible.reserve(m_Meshes.size());

	m_UpdateList.clear();
	m_UpdateList.reserve(m_Meshes.size());

	m_CreateList.clear();
	m_CreateCandidates.clear();

	const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

	// Phase A: Parallel recursive traversal — collect into update/create lists, skip heavy processing.
	//
	// Replaces the previous serial walk + the (now-removed) Phase 0 FadeNode collection. Strategy:
	//
	//   1. Serial descent (ParallelTriShapeWalker::walk) walks the tree top-down. Below any wide NiNode
	//      (children.size() >= Constants::ParallelTraversalFanoutThreshold) we do NOT recurse into its
	//      children; instead each child (with its propagated parentRefr) is appended to a flat
	//      `forkedChildren` accumulator. We continue descending through other sub-threshold subtrees to
	//      discover more wide fan-outs.
	//      - Leaves reached during this serial descent are routed directly into per-worker slot 0 (no
	//        concurrent writers since the worker tasks haven't been dispatched yet).
	//   2. After walk returns, forkedChildren is chunked into exactly `numWorkers` partitions and each
	//      chunk is dispatched as one thread pool task. Each task iterates its chunk calling serialWalk
	//      (which invokes Util::Traversal::ScenegraphTriShapes) on each child, pushing results into its
	//      exclusive per-worker slot indexed by chunkIdx+1.
	//   3. WaitAll, then serial concat into the final flat lists.
	//
	// Why chunked instead of per-child fork: the wide NiNode in a typical Skyrim frame has ~1,254 BSFadeNode
	// children; each child subtree contains ~2 leaves (the typical fade-pair). Forking 1,254 tasks swamps
	// the pool with mutex-protected Enqueue/TryPop churn and net regresses vs serial. Chunking to exactly
	// numWorkers coarse tasks (~157 subtrees each) makes the per-pool-task cost negligible relative to
	// per-subtree work, and sidesteps the slot-collision race that fine-grained forking caused.
	//
	// Safety: m_Meshes.find() is concurrent-read only (the map is mutated before A by DestroyMeshes
	// and after A by Phase C2). All NiAVObject virtual calls performed by the visitor are read-only on
	// stable per-frame tree nodes; no NiPointer<> smart-pointer copies occur inside the visitor (it passes
	// child.get() raw pointers), so no atomic refcount churn. The ShadowSceneNode portalGraph read is stable
	// by the time Update() runs. Each forked chunk writes to an exclusive per-worker slot, so per-worker
	// vectors have exactly one writer — no push_back race.
	{
		const size_t numWorkers = std::max<size_t>(1, m_ThreadPool->GetThreadCount());
		// One slot per forked chunk task (slots 1..numWorkers) plus slot 0 reserved for the main-thread
		// serial descent. Total slots = numWorkers + 1.
		const size_t numSlots = numWorkers + 1;

		m_PerWorkerUpdateList.assign(numSlots, {});
		m_PerWorkerCreateList.assign(numSlots, {});
		m_PerWorkerCurrentVisible.assign(numSlots, {});

		for (auto& v : m_PerWorkerUpdateList) v.reserve(256);
		for (auto& v : m_PerWorkerCreateList) v.reserve(64);
		for (auto& v : m_PerWorkerCurrentVisible) v.reserve(256);

		auto worldRootNode = Util::Adapter::GetWorldRootNode();

		// First person view
		// Why? The first person node is always hidden, except during first person view rendering where it is unhid for culling + rendering
		RE::NiAVObject* firstPersonRoot = m_DrawFirstPerson ? Util::Adapter::GetFirstPerson3D(RE::PlayerCharacter::GetSingleton()) : nullptr;

		// Flat accumulator of wide-NiNode children to be chunked across workers post-descent.
		// Each entry is a (child object, propagated parentRefr) ready to be handed to ProcessSubtree.
		eastl::vector<eastl::pair<RE::NiAVObject*, RE::TESObjectREFR*>> forkedChildren;

		ParallelTriShapeWalker walker{ this, &forkedChildren, firstPersonRoot };
		walker.Walk(worldRootNode);

		// Dispatch the accumulated wide-fan-out children as exactly numWorkers chunked tasks. Each task
		// iterates its chunk calling ProcessSubtree on each subtree; per-worker output slot is chunkIdx+1
		// (slot 0 is reserved for the main-thread walk above).
		const size_t totalForked = forkedChildren.size();
		if (totalForked > 0) {
			const size_t chunkSize = (totalForked + numWorkers - 1) / numWorkers;
			for (size_t chunkIdx = 0; chunkIdx < numWorkers; ++chunkIdx) {
				const size_t start = chunkIdx * chunkSize;
				const size_t end = std::min(start + chunkSize, totalForked);
				if (start >= end)
					continue;

				const size_t workerIdx = chunkIdx + 1; // slots 1..numWorkers

				m_ThreadPool->Enqueue([&, start, end, workerIdx]() {
					for (size_t i = start; i < end; ++i) {
						auto& [child, refr] = forkedChildren[i];
						walker.ProcessSubtree(child, refr, workerIdx);
					}
				});
			}

			// Drain all chunked tasks before concatenating per-worker output.
			m_ThreadPool->WaitAll();
		}

		// [PhaseA-DBG] Inspect per-worker output for null bsTriShape entries that somehow landed in the
		// create list. We log the slot, the index within the slot, and the paired refr so we can later
		// match the source in the visitor log above.
		for (size_t slot = 0; slot < m_PerWorkerCreateList.size(); ++slot) {
			const auto& w = m_PerWorkerCreateList[slot];
			for (size_t i = 0; i < w.size(); ++i) {
				if (!w[i].first) {
					logger::critical("[PhaseA-DBG] concat: m_PerWorkerCreateList[{0}][{1}] has NULL bsTriShape; refr={2:p}",
					                 slot, i, static_cast<const void*>(w[i].second));
				}
			}
		}

		// Serial concat into the final flat lists, preserving within-worker DFS order. Phase B/D/G are all
		// order-independent so worker concatenation order does not affect correctness.
		for (auto& w : m_PerWorkerUpdateList) {
			m_UpdateList.insert(m_UpdateList.end(), w.begin(), w.end());
			w.clear();
		}
		for (auto& w : m_PerWorkerCreateList) {
			m_CreateList.insert(m_CreateList.end(), w.begin(), w.end());
			w.clear();
		}
		for (auto& w : m_PerWorkerCurrentVisible) {
			m_CurrentVisible.insert(m_CurrentVisible.end(), w.begin(), w.end());
			w.clear();
		}
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

			mesh->Update(commandList);
			mesh->SetHidden(false);
			mesh->CommitDirtyFlags();

			if (!mesh->AsSubIndexMesh()) {	
				const bool ownerChanged = mesh->SetOwner(refr);
				auto cluster = mesh->GetCluster();

				BLASCluster* targetCluster = IsSpatialCandidate(mesh, refr)
					? GetOrCreateSpatialCluster(mesh)
					: GetOrCreateCluster(refr, mesh->GetTriShape());

				if (ownerChanged || !cluster || cluster != targetCluster) {
					if (cluster) {
						cluster->RemoveMember(mesh);
						MarkClusterDirty(cluster);
					}

					targetCluster->AddMember(mesh);
					MarkClusterDirty(targetCluster);
				}
			}
		};

		auto doFilter = [&](size_t start, size_t end, eastl::vector<MeshCreateCandidate>& out) {
			for (size_t i = start; i < end; ++i) {
				auto& [bsTriShape, refr] = m_CreateList[i];

				if (!bsTriShape) {
					logger::critical("[PhaseB-DBG] doFilter[{0},{1}) at i={2} found NULL bsTriShape; refr={3:p}; m_CreateList.size()={4}",
					                 start, end, i, static_cast<const void*>(refr), m_CreateList.size());
					continue;
				}

				if (!Util::Adapter::IsValidTriShape(bsTriShape))
					continue;

				const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);
				auto* shaderProperty = geometryData.shaderProperty;
				if (!shaderProperty)
					continue;

				const auto materialType = static_cast<uint32_t>(shaderProperty->GetMaterialType());
				const bool isLightingShader = (materialType == static_cast<uint32_t>(RE::BSShaderMaterial::Type::kLighting));
				const bool isEffectShader = (materialType == static_cast<uint32_t>(RE::BSShaderMaterial::Type::kEffect));
				const bool isWaterShader = (materialType == static_cast<uint32_t>(RE::BSShaderMaterial::Type::kWater));

				// Skip alpha blended effects (particles and effects)
				auto* alphaProperty = geometryData.alphaProperty;
				const bool isAlphaBlend = Util::Adapter::GetAlphaBlending(alphaProperty);
				bool validEffect = isEffectShader && !isAlphaBlend;

#if defined(FALLOUT4)
				// Glass
				validEffect |= isEffectShader && shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kEnvMap);
#endif

#if defined(SKYRIM)
				// Exclude procedural and displacement water
				if (isWaterShader) {
					auto waterShaderProperty = reinterpret_cast<RE::BSWaterShaderProperty*>(shaderProperty);
					const auto waterFlags = waterShaderProperty->waterFlags.underlying();

					if (waterFlags & WaterFlags::kProcedural || waterFlags & WaterFlags::kDisplacement)
						continue;
				}
#endif

				if (!isLightingShader && !validEffect && !isWaterShader)
					continue;

				// Exclude tree lod and grass for now
				const auto shaderPropertyRTTI = shaderProperty->GetRTTI();
				if (shaderPropertyRTTI == Constants::rtti::BSDistantTreeShaderProperty.get() ||
				    shaderPropertyRTTI == Constants::rtti::BSGrassShaderProperty.get())
					continue;

				if (Util::Geometry::IsBlocklisted(bsTriShape->name.c_str()))
					continue;

				const bool skinned = Util::Adapter::IsSkinned(geometryData);
				if (!skinned) {
					const auto& trishapeData = Util::Adapter::GetTrishapeRuntimeData(bsTriShape);
					if (trishapeData.vertexCount == 0 || trishapeData.triangleCount == 0)
						continue;
				}

				const auto rendererData = Util::Adapter::GetRendererData(geometryData);
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
			auto [it2, inserted] = m_Meshes.emplace(bsTriShape, eastl::move(created));
			if (inserted) {
				auto mesh = it2->second.get();

				mesh->SetLastVisitedFrame(frameIndex);
				mesh->Update(commandList);
				mesh->CommitDirtyFlags();
				m_CurrentVisible.push_back(mesh);

				// SubIndexMesh: not a member of any cluster itself; the K SubIndexSegmentMesh
				// children will be added to their own clusters by SubIndexMesh::Update.
				if (!mesh->AsSubIndexMesh()) {
					BLASCluster* cluster = IsSpatialCandidate(mesh, refr)
						? GetOrCreateSpatialCluster(mesh)
						: GetOrCreateCluster(refr, bsTriShape);
					cluster->AddMember(mesh);
					MarkClusterDirty(cluster);
				}
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

			mesh->CommitDirtyFlags();
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
	m_SpatialClusters.Prune([this](BLASCluster* cluster) {
		if (cluster && cluster->Empty()) {
			m_DirtyClusters.erase(cluster);
			return true;
		}
		return false;
	});
	removeEmptyClusters(m_OrphanClusters);
	removeEmptyClusters(m_SubIndexSegmentClusters);

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseF-DropClusters", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase F2: RTX Best Practices - persistent spatial partition maintenance.
	//
	// Static non-actor meshes live in spatial cell clusters (stable grid keys). This pass re-partitions
	// them lazily: only clusters that were marked dirty this frame (new members, membership/visibility
	// changes, transform changes) are re-evaluated, plus a slow periodic sweep to catch drift. Once a
	// cluster is partitioned its dirty flags clear after the BLAS build, so membership stays stable
	// across frames - no per-frame split/merge churn (the previous implementation recomputed the entire
	// partition every frame, fighting doUpdate's owner routing and forcing a full BLAS rebuild every frame).
	{
		eastl::vector<BLASCluster*> spatialClusters;
		spatialClusters.reserve(m_SpatialClusters.Size());
		m_SpatialClusters.CollectAll(spatialClusters);

		const bool evaluateAll = (m_SpatialMaintenanceCounter++ % SPATIAL_MAINTENANCE_INTERVAL == 0);

		// Snapshot spatial clusters to re-evaluate (subdivide + merge candidates). Normally only clusters
		// marked dirty this frame (new/removed members, visibility or transform changes). On the periodic
		// sweep, every spatial cluster is re-evaluated (cheap CPU checks; membership changes only when the
		// fill/overlap criteria actually fire, so the partition converges and stays stable between sweeps).
		eastl::vector<BLASCluster*> candidates;
		candidates.reserve(m_DirtyClusters.size() + (evaluateAll ? spatialClusters.size() : 0));
		eastl::hash_set<BLASCluster*> candidateSet;
		auto addCandidate = [&](BLASCluster* cluster) {
			if (cluster && cluster->IsSpatial() && !cluster->Empty() && candidateSet.insert(cluster).second)
				candidates.push_back(cluster);
		};
		for (auto* cluster : m_DirtyClusters)
			addCandidate(cluster);
		if (evaluateAll) {
			for (auto* cluster : spatialClusters)
				addCandidate(cluster);
		}

		// 1. Figure 1: Sparsity subdivision. Cells whose members are sparse relative to their enclosing
		// AABB are re-homed into finer octant sub-cells (level + 1). Persisted: the sub-cells are only
		// re-evaluated once they themselves become dirty.
		for (auto* cluster : candidates) {
			if (cluster->GetPartitionLevel() >= SPATIAL_MAX_LEVEL)
				continue;

			const auto& members = cluster->GetMembers();
			if (members.size() < 2)
				continue;

			eastl::vector<AABB> memberAABBs;
			memberAABBs.reserve(members.size());
			AABB enclosingAABB;

			for (const auto* mesh : members) {
				if (!mesh->IsHidden()) {
					memberAABBs.push_back(mesh->GetWorldAABB());
					enclosingAABB.Union(mesh->GetWorldAABB());
				}
			}

			if (memberAABBs.size() < 2 || !enclosingAABB.IsValid())
				continue;

			// Subdivide when the group is sparse (NVIDIA Fig 1) OR its enclosing AABB is simply too
			// large for a single BLAS instance. The diagonal bound directly caps instance AABB size,
			// tightening the TLAS even for dense-but-wide groups.
			const float fillRatio = Clustering::ComputeFillRatio(memberAABBs, enclosingAABB);
			const float clusterDiag = enclosingAABB.GetExtents().Length();
			if (fillRatio < Clustering::SPARSITY_FILL_RATIO_THRESHOLD || clusterDiag > Clustering::MAX_CLUSTER_AABB_DIAGONAL) {
				auto membersCopy = members;
				for (auto* mesh : membersCopy) {
					if (mesh->IsHidden())
						continue;

					cluster->RemoveMember(mesh);
					auto* subCluster = GetOrCreateSpatialClusterAt(mesh, static_cast<uint8_t>(cluster->GetPartitionLevel() + 1));
					if (subCluster == cluster) {
						cluster->AddMember(mesh); // cannot happen (different key level), but stay safe
					} else {
						subCluster->AddMember(mesh);
						MarkClusterDirty(subCluster);
					}
				}
				MarkClusterDirty(cluster);
				m_DebugSubdivideCount++;
			}
		}

		// 2. Figure 2: Overlap merge. Spatial clusters whose world AABBs overlap significantly are merged
		// into a single BLAS (identity instance transform; per-geometry transforms place them in world space).
		// Only pairs involving a dirty cluster are considered, unless the periodic sweep is evaluating all.
		{
			// Rebuild candidates after subdivision (a subdivided cell may now be empty).
			candidateSet.clear();
			for (auto* cluster : candidates) {
				if (!cluster->Empty())
					candidateSet.insert(cluster);
			}

			eastl::vector<BLASCluster*> nonEmptySpatial;
			nonEmptySpatial.reserve(spatialClusters.size());
			for (auto* cluster : spatialClusters) {
				if (cluster && !cluster->Empty())
					nonEmptySpatial.push_back(cluster);
			}

			if (nonEmptySpatial.size() > 1) {
				Clustering::SpatialAABBBroadphase broadphase;
				for (uint32_t i = 0; i < static_cast<uint32_t>(nonEmptySpatial.size()); ++i)
					broadphase.Insert(i, nonEmptySpatial[i]->GetWorldAABB());

				Clustering::DSU dsu(nonEmptySpatial.size());
				for (const auto& [cellKey, indices] : broadphase.grid) {
					if (indices.size() < 2)
						continue;

					for (size_t a = 0; a < indices.size(); ++a) {
						const uint32_t idxA = indices[a];
						const auto& boxA = nonEmptySpatial[idxA]->GetWorldAABB();
						if (!boxA.IsValid())
							continue;

						if (!evaluateAll && !candidateSet.contains(nonEmptySpatial[idxA]))
							continue;

						for (size_t b = a + 1; b < indices.size(); ++b) {
							const uint32_t idxB = indices[b];
							if (dsu.Find(idxA) == dsu.Find(idxB))
								continue;

							const auto& boxB = nonEmptySpatial[idxB]->GetWorldAABB();
							if (!boxB.IsValid())
								continue;

							if (boxA.OverlapRatio(boxB) >= Clustering::OVERLAP_RATIO_THRESHOLD)
								dsu.Union(idxA, idxB);
						}
					}
				}

				// Group components merged by DSU
				eastl::hash_map<int32_t, eastl::vector<uint32_t>> mergedGroups;
				for (uint32_t i = 0; i < static_cast<uint32_t>(nonEmptySpatial.size()); ++i)
					mergedGroups[dsu.Find(i)].push_back(i);

				for (auto& [root, group] : mergedGroups) {
					if (group.size() < 2)
						continue;

					// Pick the first non-empty cluster as primary (a member may have been subdivided this frame).
					uint32_t primaryIdx = group.front();
					for (const auto idx : group) {
						if (!nonEmptySpatial[idx]->Empty()) {
							primaryIdx = idx;
							break;
						}
					}

					auto* primaryCluster = nonEmptySpatial[primaryIdx];
					for (const auto idx : group) {
						if (idx == primaryIdx)
							continue;

						auto* secondaryCluster = nonEmptySpatial[idx];
						if (secondaryCluster->Empty())
							continue;

						auto membersToMove = secondaryCluster->GetMembers();
						for (auto* m : membersToMove) {
							secondaryCluster->RemoveMember(m);
							primaryCluster->AddMember(m);
						}
						MarkClusterDirty(secondaryCluster);
						m_DebugMergeCount++;
					}
					primaryCluster->UpdateTransform();
					MarkClusterDirty(primaryCluster);
				}
			}
		}
	}

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseF2-SplitMerge", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase G (parallel): each cluster atomically reserves its own offsets in MeshData / InstanceData.
	{
		m_AllClusters.clear();
		m_AllClusters.reserve(
			m_OwnerClusters.size() +
			m_SpatialClusters.Size() +
			m_OrphanClusters.size() +
			m_SubIndexSegmentClusters.size());

		for (auto& [_, cluster] : m_OwnerClusters)
			m_AllClusters.push_back(cluster.get());

		m_SpatialClusters.CollectAll(m_AllClusters);

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

						const uint32_t meshCount = cluster->Update();

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

						// Write remap entries: packed (instanceID << 16) | geometrySlot into the ByteAddress remap buffer
						const auto& geometrySlots = cluster->GetGeometrySlots();
						for (uint32_t j = 0; j < meshCount; j++) {
							const uint32_t remapIdx = firstMesh + j;
							m_MeshSlotRemapData[remapIdx] = static_cast<uint32_t>(geometrySlots[j]) | (instanceIndex << 16);
						}

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

	if (m_NumMeshes > 0) {
		// Write remap data: contiguous uint2 entries at the start of the remap buffer
		commandList->writeBuffer(m_MeshSlotRemapBuffer.current(), m_MeshSlotRemapData.data(), m_NumMeshes * 4ull, 0);
	}

	if (m_NumInstances > 0)
		commandList->writeBuffer(GetInstanceBuffer(), m_InstanceData.data(), m_NumInstances * sizeof(InstanceData));

	m_MeshManager->Flush(commandList);

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::BufferWrites", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});

		const auto totalEnd = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::Total", 0.0f, std::chrono::duration<float, std::milli>(totalEnd - updateStart).count()});
	}

	if (m_SpatialMaintenanceCounter % 120 == 0) {
		uint32_t actorClusterCount = 0;
		uint32_t staticOwnerClusterCount = 0;
		for (const auto& [refr, cluster] : m_OwnerClusters) {
			if (cluster && !cluster->Empty()) {
				if (Util::IsActor(refr))
					actorClusterCount++;
				else
					staticOwnerClusterCount++;
			}
		}

		// Spatial partition breakdown: level distribution, member-count buckets, AABB tightness.
		eastl::vector<BLASCluster*> spatialClusters;
		spatialClusters.reserve(m_SpatialClusters.Size());
		m_SpatialClusters.CollectAll(spatialClusters);

		uint32_t levelCounts[SPATIAL_MAX_LEVEL + 1] = { 0, 0, 0 };
		uint32_t bucket1 = 0, bucket2_4 = 0, bucket5_10 = 0, bucket10 = 0;
		uint32_t invalidAABB = 0;
		float avgDiagonal = 0.0f;
		float maxDiagonal = 0.0f;
		uint32_t counted = 0;
		for (auto* c : spatialClusters) {
			if (!c || c->Empty())
				continue;

			const uint8_t lvl = c->GetPartitionLevel() <= SPATIAL_MAX_LEVEL ? c->GetPartitionLevel() : SPATIAL_MAX_LEVEL;
			levelCounts[lvl]++;

			const size_t n = c->GetMembers().size();
			if (n == 1)
				bucket1++;
			else if (n <= 4)
				bucket2_4++;
			else if (n <= 10)
				bucket5_10++;
			else
				bucket10++;

			const AABB& box = c->GetWorldAABB();
			if (box.IsValid()) {
				const float d = box.GetExtents().Length();
				avgDiagonal += d;
				maxDiagonal = std::max(maxDiagonal, d);
				counted++;
			} else {
				invalidAABB++;
			}
		}
		if (counted > 0)
			avgDiagonal /= static_cast<float>(counted);

		logger::info("Clusters: Total Instances: {}, Actor Clusters: {}, Static REFR Clusters: {}, Spatial Clusters: {}, Orphan/Split Clusters: {}, SubIndex Clusters: {}, Total Meshes: {}",
			m_NumInstances,
			actorClusterCount,
			staticOwnerClusterCount,
			m_SpatialClusters.Size(),
			m_OrphanClusters.size(),
			m_SubIndexSegmentClusters.size(),
			m_NumMeshes);

		logger::info("SpatialPartition: {} clusters (L0:{}, L1:{}, L2:{}), members(1:{}, 2-4:{}, 5-10:{}, 10+:{}, invalidAABB:{}), AABB diag avg {:.0f} max {:.0f}",
			counted,
			levelCounts[0], levelCounts[1], levelCounts[2],
			bucket1, bucket2_4, bucket5_10, bucket10, invalidAABB,
			avgDiagonal, maxDiagonal);

		logger::info("BLASBuilds: Rebuild {} Update {} Skip {} | Spatial Subdivide {} Merge {}",
			m_DebugRebuildCount, m_DebugUpdateCount, m_DebugSkipCount, m_DebugSubdivideCount, m_DebugMergeCount);

		// Top-N largest spatial clusters with member mesh AABB details (diagnostic).
		{
			struct BigCluster {
				float diag;
				BLASCluster* c;
			};
			eastl::vector<BigCluster> biggest;
			biggest.reserve(9);
			for (auto* c : spatialClusters) {
				if (!c || c->Empty())
					continue;
				const AABB& box = c->GetWorldAABB();
				if (!box.IsValid())
					continue;
				const float d = box.GetExtents().Length();
				auto it = biggest.begin();
				while (it != biggest.end() && it->diag >= d)
					++it;
				biggest.insert(it, { d, c });
				if (biggest.size() > 8)
					biggest.pop_back();
			}

			for (const auto& [d, c] : biggest) {
				logger::info("BIG {} level {} members {} diag {:.0f}:", c->m_Name.c_str(), c->GetPartitionLevel(), c->GetMembers().size(), d);
				uint32_t shown = 0;
				for (const auto* m : c->GetMembers()) {
					if (shown >= 5)
						break;
					const AABB& wb = m->GetWorldAABB();
					const AABB& lb = m->GetLocalAABB();
					logger::info("    [{}] {} worldDiag {:.0f} localDiag {:.0f} center ({:.0f},{:.0f},{:.0f})",
						shown,
						m->GetName().c_str(),
						wb.IsValid() ? wb.GetExtents().Length() : 0.0f,
						lb.IsValid() ? lb.GetExtents().Length() : 0.0f,
						wb.IsValid() ? wb.GetCenter().x : 0.0f,
						wb.IsValid() ? wb.GetCenter().y : 0.0f,
						wb.IsValid() ? wb.GetCenter().z : 0.0f);
					shown++;
				}
			}
		}

		m_DebugSubdivideCount = 0;
		m_DebugMergeCount = 0;
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

bool SceneGraph::IsSpatialCandidate(BaseMesh* mesh, RE::TESObjectREFR* refr) const
{
	if (!mesh)
		return false;

	if (refr && Util::IsActor(refr))
		return false;

	if (mesh->HasFlag(BaseMesh::Flags::LandLOD4))
		return false;

	if (mesh->GetType() != BaseMesh::Type::Default)
		return false;

	// Terrain/landscape ground quads are excluded from the spatial partition: their world AABBs dwarf
	// the 1024-unit cells (a ~2048-unit quad's transform scales its local extent up to ~32x, so a single
	// quad's AABB can span the whole map). Grouping them spatially only bloats nearby cell clusters.
	// They keep per-owner clustering, as on the original branch.
	if (auto* triShape = mesh->GetTriShape()) {
		const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(triShape);
		if (auto* shaderProperty = geometryData.shaderProperty) {
			if (auto* material = shaderProperty->material) {
				switch (material->GetFeature())
				{
#if defined(SKYRIM)
				case RE::BSShaderMaterial::Feature::kMultiTexLand:
				case RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend:
				case RE::BSShaderMaterial::Feature::kLODLand:
				case RE::BSShaderMaterial::Feature::kLODLandNoise:
#elif defined(FALLOUT4)
				case RE::BSShaderMaterial::Feature::kLandscape:
				case RE::BSShaderMaterial::Feature::kLODLandscapeBlend:
				case RE::BSShaderMaterial::Feature::kLODLandscape:
				case RE::BSShaderMaterial::Feature::kLODLandscapeNoise:
#endif
					return false;
				default:
					break;
				}
			}
		}
	}

	// Robust catch-all for terrain/ground quads (and any oversized ground mesh): their local AABB
	// diagonal (measured 4k-6k units) far exceeds any normal static object (buildings/trees < ~2k).
	// Such meshes can't benefit from spatial grouping and only bloat cell cluster AABBs.
	const AABB& local = mesh->GetLocalAABB();
	if (local.IsValid() && local.GetExtents().Length() > 3000.0f)
		return false;

	// Also exclude meshes whose WORLD AABB is very large even though their local geometry is small
	// (e.g. transform-scaled ground/water/ruin pieces). Their huge instance AABB would dominate any
	// cell cluster they land in, so they keep per-owner clustering instead.
	const AABB& world = mesh->GetWorldAABB();
	if (world.IsValid() && world.GetExtents().Length() > 8000.0f)
		return false;

	return true;
}

BLASCluster* SceneGraph::GetOrCreateSpatialCluster(BaseMesh* mesh)
{
	// Keep the mesh in its current spatial cluster if one is already assigned. This is what makes the
	// partition stable frame-to-frame: routing never drags a partitioned static back to an owner cluster,
	// and a mesh stays put unless its cluster is pruned or the periodic sweep re-homes it.
	if (auto* cluster = mesh->GetCluster(); cluster && cluster->IsSpatial())
		return cluster;

	return GetOrCreateSpatialClusterAt(mesh, 0);
}

BLASCluster* SceneGraph::GetOrCreateSpatialClusterAt(BaseMesh* mesh, uint8_t level)
{
	const auto center = mesh->GetWorldAABB().GetCenter();
	const auto spatialKey = GetSpatialKey(RE::NiPoint3(center.x, center.y, center.z), level);

	auto* cluster = m_SpatialClusters.FindOrEmplace(spatialKey, [&]() {
		const auto name = std::format("SpatialCluster ({}, {}, {}) L{}", spatialKey.cellX, spatialKey.cellY, spatialKey.cellZ, spatialKey.level);
		auto newCluster = eastl::make_unique<BLASCluster>(nullptr, name.c_str());
		newCluster->m_IsSpatial = true;
		newCluster->m_PartitionLevel = spatialKey.level;
		MarkClusterDirty(newCluster.get());
		return newCluster;
	});

	return cluster;
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
	m_DebugRebuildCount = 0;
	m_DebugUpdateCount = 0;
	m_DebugSkipCount = 0;

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

uint32_t SceneGraph::AllocateMeshIndex()
{
	return m_MeshManager->AllocateMeshIndex();
}

uint32_t SceneGraph::AllocateGeometryIndex()
{
	return m_MeshManager->AllocateGeometryIndex();
}

void SceneGraph::WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform)
{
	m_MeshManager->WriteTransformData(index, transform, prevTransform);
}
