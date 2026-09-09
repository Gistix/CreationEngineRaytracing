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
#include "Core/Mesh/InstancedMesh.h"
#include "Core/BLASInstanceCluster.h"
#include "Core/ParallelTriShapeWalker.h"

#include <cassert>
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
	//   2. After walk returns, ParallelFor claims the accumulated subtrees in blocks (dynamic load
	//      balancing) and calls ProcessSubtree on each, pushing results into the task's own slot.
	//   3. WaitAll (inside ParallelFor), then serial concat into the final flat lists.
	//
	// Why not per-child fork: the wide NiNode in a typical Skyrim frame has ~1,254 BSFadeNode children;
	// each child subtree contains ~2 leaves (the typical fade-pair). Forking 1,254 tasks swamps the pool
	// with mutex-protected Enqueue/TryPop churn and net regresses vs serial. Claiming subtrees in blocks
	// from a few coarse tasks preserves load balance without that churn, and each task owns its output
	// slot, so there is no push_back race.
	//
	// Slot ownership: slot 0 is permanently owned by the serial descent above; ParallelFor tasks use
	// slots taskIdx + 1 (1..numWorkers). One slot == one writer for the whole phase.
	//
	// Safety: Phase A only reads the scene tree and m_Meshes (m_Meshes.find() concurrent-read; the map
	// is mutated before A by DestroyMeshes and after A by Phase C). No SceneGraph registry is mutated
	// during the phase, and the scene tree / ShadowSceneNode portalGraph are stable while Update() runs.
	// The visitor passes child.get() raw pointers (no NiPointer<> copies, so no atomic refcount churn).
	// All per-worker output vectors have a single writer.
	{
		const size_t numWorkers = std::max<size_t>(1, m_ThreadPool->GetThreadCount());
		// One slot per ParallelFor task (slots 1..numWorkers) plus slot 0 reserved for the main-thread
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

		// Traversal cost per subtree varies, so use a small grain. Output slot = taskIdx + 1 (slot 0
		// reserved for the serial walk above).
		const size_t totalForked = forkedChildren.size();
		m_ThreadPool->ParallelFor(totalForked, 16, [&](size_t taskIdx, size_t i) {
			auto& [child, refr] = forkedChildren[i];
			walker.ProcessSubtree(child, refr, taskIdx + 1);
		});

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

	// Phase B (parallel): update known meshes AND filter new meshes via thread pool (the former C1
	// candidate filtering is folded into this phase)
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
					}

					cluster = GetOrCreateCluster(refr, mesh->GetTriShape());
					cluster->AddMember(mesh);
				}
			}

			mesh->Update(commandList);
			mesh->SetHidden(false);
			mesh->CommitDirtyFlags();
		};

		const auto& settings = Scene::GetSingleton()->m_Settings;
		const bool renderTreeLOD = settings.ExperimentalSettings.RenderTreeLOD;
		const bool allowInstancedTriShape = renderTreeLOD;

		auto doFilter = [&](size_t start, size_t end, eastl::vector<MeshCreateCandidate>& out) {
			for (size_t i = start; i < end; ++i) {
				auto& [bsTriShape, refr] = m_CreateList[i];

				if (!bsTriShape)
					continue;

				if (!Util::Adapter::IsValidTriShape(bsTriShape, allowInstancedTriShape))
					continue;

				const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);
				auto* shaderProperty = geometryData.shaderProperty;
				if (!shaderProperty)
					continue;

				const auto materialType = static_cast<RE::BSShaderMaterial::Type>(shaderProperty->GetMaterialType());

				const bool isBase = (materialType == RE::BSShaderMaterial::Type::kBase);
				const bool isLightingShader = (materialType == RE::BSShaderMaterial::Type::kLighting);
				const bool isEffectShader = (materialType == RE::BSShaderMaterial::Type::kEffect);
				const bool isWaterShader = (materialType == RE::BSShaderMaterial::Type::kWater);

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

				// Let base pass, we'll filter below
				if (!isBase && !isLightingShader && !validEffect && !isWaterShader)
					continue;

				if (isBase) {
					// We're only interested in Tree LOD
					const auto shaderPropertyRTTI = shaderProperty->GetRTTI();
					if (shaderPropertyRTTI != Constants::rtti::BSDistantTreeShaderProperty.get())
						continue;
				}

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

		if (totalWork > 0) {
			// Mesh work is roughly uniform per item, so a moderate grain is fine.
			m_ThreadPool->ParallelFor(totalWork, 32, [&](size_t, size_t i) {
				doUpdate(m_UpdateList[i]);
			});
		}

		if (totalCreate > 0) {
			// Each task owns one candidate slot (single writer per slot); taskIdx is a scratch-slot
			// index, not a range of inputs.
			m_ThreadPool->ParallelFor(totalCreate, 32, [&](size_t taskIdx, size_t i) {
				doFilter(i, i + 1, m_PerWorkerCreateCandidates[taskIdx]);
			});
		}

		m_CreateCandidates.reserve(m_CreateList.size());
		for (auto& wc : m_PerWorkerCreateCandidates)
			m_CreateCandidates.insert(m_CreateCandidates.end(), wc.begin(), wc.end());
	}

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseB-Parallel", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase C (serial): GPU resource creation for validated candidates
	for (auto& [bsTriShape, refr] : m_CreateCandidates) {
		if (auto created = BaseMesh::Create(bsTriShape, commandList)) {
			created->SetOwner(refr);
			auto [it2, inserted] = m_Meshes.emplace(bsTriShape, eastl::move(created));
			if (inserted) {
				auto mesh = it2->second.get();

				// SubIndexMesh: not a member of any cluster itself; the K SubIndexSegmentMesh
				// children will be added to their own clusters by SubIndexMesh::Update.
				if (!mesh->AsSubIndexMesh()) {
					auto* cluster = GetOrCreateCluster(refr, bsTriShape);
					cluster->AddMember(mesh);
				}

				mesh->SetLastVisitedFrame(frameIndex);
				mesh->Update(commandList);
				mesh->CommitDirtyFlags();
				m_CurrentVisible.push_back(mesh);
			}
		}
	}

	if (timings) {
		const auto nowTp = std::chrono::high_resolution_clock::now();
		m_UpdateTimings.push_back({"SG::PhaseC-Create", 0.0f, std::chrono::duration<float, std::milli>(nowTp - phaseStart).count()});
		phaseStart = nowTp;
	}

	// Phase D: Hide meshes whose trishapes were not visited by the traversal this frame
	for (auto& mesh : m_PreviousVisible) {
		if (mesh->GetLastVisitedFrame() != frameIndex) {
			mesh->SetHidden(true);

			if (auto* cluster = mesh->GetCluster()) {
				cluster->RemoveMember(mesh);
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

	// Phase G (parallel): per-cluster counts, a serial prefix-sum to assign offsets, then a parallel
	// fill of the remap/instance data. No global lock is needed: offsets are assigned on the main
	// thread between two lock-free parallel passes.
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

		const size_t totalWork = m_AllClusters.size();
		m_ClusterWork.resize(totalWork);

		if (totalWork > 0) {
			// Pass 1 (parallel): each cluster computes its counts (transform/light data, desc counts).
			// cluster->Update() runs concurrently for distinct clusters; it may only touch cluster-local
			// state and thread-safe managers (see the contract on BLASCluster::Update()).
			m_ThreadPool->ParallelFor(totalWork, 32, [&](size_t, size_t i) {
				auto* cluster = m_AllClusters[i];
				auto& work = m_ClusterWork[i];
				work.meshCount = cluster->Update();
				work.instanceCount = work.meshCount ? cluster->GetInstanceCount() : 0;
			});

			// Pass 2 (serial): prefix-sum assigns each valid cluster its mesh/instance base offsets.
			m_NumMeshes = 0;
			m_NumInstances = 0;
			bool reportedMeshLimit = false;
			bool reportedInstanceLimit = false;

			for (size_t i = 0; i < totalWork; ++i) {
				auto* cluster = m_AllClusters[i];
				auto& work = m_ClusterWork[i];
				const uint32_t meshCount = work.meshCount;
				const uint32_t instCount = work.instanceCount;

				if (meshCount == 0 || instCount == 0) {
					work.firstMesh = UINT32_MAX;
					work.firstInstance = UINT32_MAX;
					continue;
				}

				if (m_NumMeshes + meshCount > Constants::NUM_MESHES_MAX) {
					cluster->SetValid(false);
					if (!reportedMeshLimit) {
						logger::critical("SceneGraph::Update - Mesh capacity ({}) reached; omitting a cluster with {} mesh entries.", Constants::NUM_MESHES_MAX, meshCount);
						reportedMeshLimit = true;
					}
					work.firstMesh = UINT32_MAX;
					work.firstInstance = UINT32_MAX;
					continue;
				}

				if (m_NumInstances + instCount > Constants::NUM_INSTANCES_MAX) {
					cluster->SetValid(false);
					if (!reportedInstanceLimit) {
						logger::critical("SceneGraph::Update - Instance capacity ({}) reached; omitting a cluster with {} instances.", Constants::NUM_INSTANCES_MAX, instCount);
						reportedInstanceLimit = true;
					}
					work.firstMesh = UINT32_MAX;
					work.firstInstance = UINT32_MAX;
					continue;
				}

				work.firstMesh = m_NumMeshes;
				work.firstInstance = m_NumInstances;
				m_NumMeshes += meshCount;
				m_NumInstances += instCount;
			}

#if !defined(NDEBUG)
			// Debug: assigned ranges must be contiguous, non-overlapping, and cover the totals exactly.
			// This is the invariant that replaced the reservation mutex, so make it easy to diagnose.
			{
				uint32_t meshCursor = 0;
				uint32_t instCursor = 0;
				for (const auto& w : m_ClusterWork) {
					if (w.firstMesh == UINT32_MAX)
						continue;
					assert(w.firstMesh == meshCursor);
					assert(w.firstInstance == instCursor);
					meshCursor += w.meshCount;
					instCursor += w.instanceCount;
				}
				assert(meshCursor == m_NumMeshes);
				assert(instCursor == m_NumInstances);
			}
#endif

			// Pass 3 (parallel): write remap entries and instance data into the disjoint assigned ranges.
			m_ThreadPool->ParallelFor(totalWork, 32, [&](size_t, size_t i) {
				auto& work = m_ClusterWork[i];
				if (work.firstMesh == UINT32_MAX)
					return;

				auto* cluster = m_AllClusters[i];
				const uint32_t meshCount = work.meshCount;
				const uint32_t instanceIndex = work.firstInstance;

				// Write remap entries: packed (instanceID << 16) | geometrySlot into the ByteAddress remap buffer
				const auto& geometrySlots = cluster->GetGeometrySlots();
				for (uint32_t j = 0; j < meshCount; j++) {
					const uint32_t remapIdx = work.firstMesh + j;
					m_MeshSlotRemapData[remapIdx] = static_cast<uint32_t>(geometrySlots[j]) | (instanceIndex << 16);
				}

				cluster->SetInstanceIndex(instanceIndex);
				cluster->WriteInstanceData(work.firstMesh, meshCount, &m_InstanceData[instanceIndex]);
			});
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

#if defined(FALLOUT4)
	logger::info("Cluster: {}, Orphan Clusters: {}, SubIndexSegment Clusters: {}", m_OwnerClusters.size(), m_OrphanClusters.size(), m_SubIndexSegmentClusters.size());
#endif
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
	{
		std::unique_lock lock(a_mutex);

		auto [it, inserted] = a_map.try_emplace(a_key, nullptr);

		if (inserted) {
			if constexpr (std::is_same_v<Key, RE::BSTriShape*>) {
				if (Util::Adapter::AsMultiStreamInstanceTriShape(a_key))
					it->second = eastl::make_unique<BLASInstanceCluster>(a_owner);
				else
					it->second = eastl::make_unique<BLASCluster>(a_owner);
			} else
				it->second = eastl::make_unique<BLASCluster>(a_owner);
		}

		result = it->second.get();
	}

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

void SceneGraph::BuildClusters(nvrhi::ICommandList* commandList)
{
	// Visit every cluster with pending dirty flags. Flags are set on membership changes (Mesh) and
	// mesh flag commits, and only cleared inside BuildUpdate - so non-None always means a build is
	// needed. m_AllClusters was rebuilt in Phase G after empty clusters were dropped. The scan runs
	// on the render thread after Phase WaitAll, so flags are not being written.
	for (auto* cluster : m_AllClusters)
		if (cluster->m_DirtyFlags != DirtyFlags::None)
			cluster->BuildUpdate(commandList, this);
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

void SceneGraph::RegisterBlock(RE::BGSDistantTreeBlock* block)
{
	if (block->treeGroups.empty())
		return;

	for (auto& group: block->treeGroups)
	{
		if (!group->geometry)
			continue;

		auto* geometry = group->geometry.get();

		std::scoped_lock lock(m_DistantTreeMutex);

		auto [it, emplaced] = m_DistantTree.try_emplace(geometry);

		for (auto& instanceData : group->instances)
		{
			it->second.push_back(instanceData);
		}
	}
}

void SceneGraph::ReleaseBlock(RE::BGSDistantTreeBlock* block)
{
	if (block->treeGroups.empty())
		return;

	for (auto& group : block->treeGroups)
	{
		if (!group->geometry)
			continue;

		auto* geometry = group->geometry.get();

		std::scoped_lock lock(m_DistantTreeMutex);

		m_DistantTree.erase(geometry);
	}
}

eastl::vector<RE::BGSDistantTreeBlock::InstanceData> SceneGraph::GetBlockInstanceData(RE::BSTriShape* triShape)
{
	std::scoped_lock lock(m_DistantTreeMutex);

	auto it = m_DistantTree.find(triShape);
	if (it == m_DistantTree.end())
		return {};

	return it->second;
}
