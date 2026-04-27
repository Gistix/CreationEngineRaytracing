#include "SceneGraph.h"

#include "Scene.h"

#include "core/Mesh.h"

#include "Renderer.h"
#include "Util.h"
#include "ShaderUtils.h"

#include "Types/RE/RE.h"
#include "Types/CommunityShaders/LightLimitFix.h"
#include "Types/CommunityShaders/ISLCommon.h"

#include "Pass/Raytracing/Common/Skinning.h"

void SceneGraph::Initialize()
{
	m_CurrentAccumulator = { REL::RelocationID(527650, 414600) };

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
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2).setSize(UINT_MAX)
		};

		m_VertexDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
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

		m_DynamicVertexDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
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

	// Vertex copy descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2).setSize(UINT_MAX)
		};

		m_VertexCopyDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	// Vertex write descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0).setSize(UINT_MAX)
		};

		m_VertexWriteDescriptors = eastl::make_unique<BindlessTable>(device, bindlessLayoutDesc, true);
	}

	// Previous position SRV descriptor table (for reading prev positions in RT shaders)
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_MESHES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5).setSize(UINT_MAX)
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

void SceneGraph::UpdateLights(nvrhi::ICommandList* commandList)
{
	auto& mainSSNRuntimeData = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]->GetRuntimeData();

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

		if (niLight->GetFlags().any(RE::NiAVObject::Flag::kHidden))
			light.m_Active = false;

		if (bsLight->IsShadowLight())
		{
			auto* shadowLight = reinterpret_cast<RE::BSShadowLight*>(bsLight);

			if (shadowLight->GetRuntimeData().maskIndex == 255)
				light.m_Active = false;
		}

		auto& runtimeData = niLight->GetLightRuntimeData();

		auto flags = std::bit_cast<LightLimitFix::LightFlags>(runtimeData.ambient.red);

		if (flags & LightLimitFix::LightFlags::Disabled)
			light.m_Active = false;

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

				// NiSpotLight stores outerSpotAngle (half-angle in degrees) right after NiPointLight data
				// NiPointLight size: 0x150 (SSE). NiSpotLight adds: outerSpotAngle at 0x14C, innerSpotAngle at 0x150
				// These are accessible as POINT_LIGHT_RUNTIME_DATA is at 0x140, 3 floats (12 bytes) = ends at 0x14C
				// Then: spotOuterAngle at 0x14C, spotInnerAngle at 0x150, spotExponent at 0x154
				auto* pointLightData = reinterpret_cast<const float*>(&static_cast<RE::NiPointLight*>(niLight)->GetPointLightRuntimeData());
				float outerAngleDeg = pointLightData[3]; // After constAtten, linearAtten, quadAtten
				float innerAngleDeg = pointLightData[4];

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

			if (flags & LightLimitFix::LightFlags::InverseSquare) {
				lightData.Flags |= LightFlags::ISL;

				auto* extData = ISLCommon::RuntimeLightDataExt::Get(niLight);

				lightData.Fade *= 4.0f;
				lightData.FadeZone = 1.f / (lightData.Radius * std::clamp(ISLCommon::FadeZoneBase * lightData.InvRadius, 0.f, 1.f));
				lightData.SizeBias = ISLCommon::ScaledUnitsSq * extData->size * extData->size * 0.5f;
			}

			if (flags & LightLimitFix::LightFlags::Linear)
				lightData.Flags |= LightFlags::LinearLight;
		}

		numLights++;

		if (numLights >= Constants::LIGHTS_MAX) {
			logger::error("SceneGraph::UpdateLights - Number of lights {} exceeds the maximum of {}", numLights, Constants::LIGHTS_MAX);
			break;
		}
	}

	commandList->writeBuffer(m_LightBuffer, m_LightData.data(), numLights * sizeof(LightData));
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
		if (!block->loaded || !block->attached)
			continue;

		ref.UpdateVisibility(block->chunk);
	}

	for (auto& [block, ref] : m_ObjectLODInstances)
	{
		if (!block->loaded || !block->attached)
			continue;

		ref.UpdateVisibility(block->chunk);
	}
}

void SceneGraph::Update(nvrhi::ICommandList* commandList)
{
	UpdateLights(commandList);

	m_NumMeshes = 0;
	m_NumInstances = 0;

	eastl::array<uint8_t, Constants::INSTANCE_LIGHTS_MAX> lights;

	for (auto& instance : m_Instances)
	{
		instance->Update(m_NumInstances);

		if (instance->IsHidden())
			continue;

		bool isPlayer = Util::IsPlayerFormID(instance->m_FormID);

		// Update if applicabe and queue skinning/dynamic update
		instance->model->Update(instance->m_Node, isPlayer);

		uint32_t firstMeshIndex = m_NumMeshes;

		// Get mesh data
		instance->model->SetData(m_MeshData.data(), m_NumMeshes);

		// No visible meshes in instance
		bool hiddenModel = m_NumMeshes == firstMeshIndex;
		instance->SetHiddenModel(hiddenModel);
		
		if (instance->SkipAS())
			continue;

		uint8_t numLights = 0u;

		for (auto& [bsLight, light] : m_Lights)
		{
			if (light.m_Instances.find(instance.get()) == light.m_Instances.end())
				continue;

			lights[numLights] = light.m_Index;
			numLights++;

			if (numLights > Constants::INSTANCE_LIGHTS_MAX) {
				logger::error("SceneGraph::Update - Number of lights per instance of {} exceeds the maximum of {}", numLights, Constants::INSTANCE_LIGHTS_MAX);
				break;
			}
		}

		m_InstanceData[m_NumInstances] = {
			instance->m_Transform,
			instance->m_PrevTransform,
			InstanceLightData(lights.data(), numLights),
			firstMeshIndex
		};

		m_NumInstances++;
	}

	if (m_NumMeshes > 0)
		commandList->writeBuffer(m_MeshBuffer, m_MeshData.data(), m_NumMeshes * sizeof(MeshData));

	if (m_NumInstances > 0)
		commandList->writeBuffer(m_InstanceBuffer, m_InstanceData.data(), m_NumInstances * sizeof(InstanceData));
}

void SceneGraph::ClearDirtyStates()
{
	for (auto& [path, model] : m_Models)
	{
		model->ClearDirtyState();
	}

	for (auto& instance : m_Instances)
	{
		instance->ClearDirtyState();
	}
}

void SceneGraph::CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root)
{
	if (!root) {
		logger::warn("[RT] CreateModel - NULL root object for model: {}", model ? model : "unknown");
		return;
	}

	// TODO: Proper Model transform update, this whole section feels like hack
	const REL::Relocation<const RE::NiRTTI*> rtti{ RE::NiMultiTargetTransformController::Ni_RTTI };
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
			parents.emplace(target->parent);

			if (!emplaced)
				continue;

			createModels += CreateModelInternal(form, std::format("{}_{}", model, target->name.c_str()).c_str(), target);
		}

		for (auto* parent : parents) {
			for (auto& child : parent->GetChildren()) {
				if (targets.find(child.get()) != targets.end())
					continue;

				createModels += CreateModelInternal(form, std::format("{}_{}_{}", model, child->name.c_str(), child->parentIndex).c_str(), child.get());
			}
		}

		if (createModels > 0)
			return;
	}

	CreateModelInternal(form, model, root);
}

void SceneGraph::CreateActorModel(RE::Actor* actor, RE::NiAVObject* root, bool firstPerson)
{
	auto name = std::format("{}{}_{:0X}", actor->GetName(), firstPerson ? "_1stPerson" : "", actor->GetFormID());

	auto* biped = actor->GetBiped(firstPerson).get();

	logger::debug("SceneGraph::CreateActorModel - {}", name);

	if (biped) {
		eastl::vector<eastl::unique_ptr<Mesh>> meshes;
		eastl::vector<Mesh*> faceMeshes;
		eastl::array<eastl::vector<Mesh*>, RE::BIPED_OBJECTS::kTotal> bipedMeshes;

		auto createAppendMeshes = [&](RE::TESForm* form, RE::NiAVObject* object, int i = -1) {
			logger::debug("Appending {}: {}", magic_enum::enum_name(form->GetFormType()), object->name);

			for (auto& mesh : CreateMeshes(form, object))
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
				createAppendMeshes(actor, headNode);

		for (uint32_t i = 0; i < RE::BIPED_OBJECTS::kTotal; i++)
		{
			const auto& object = biped->objects[i];

			if (!object.item)
				continue;

			if (!object.partClone)
				continue;

			logger::debug("\tSceneGraph::CreateActorModel - {}", magic_enum::enum_name(static_cast<RE::BIPED_OBJECT>(i)));
			createAppendMeshes(object.item, object.partClone.get(), i);
		}

		auto object = actor->Get3D(firstPerson);

		if (auto* model = CommitModel(name.c_str(), object, actor, meshes)) {
			AddInstance(actor->GetFormID(), object, model);
			m_Actors.try_emplace(actor->GetFormID(), ActorReference(actor, firstPerson, faceMeshes, bipedMeshes));
		}
	}
	else {
		Util::Traversal::ScenegraphFadeNodes(root, [&](RE::BSFadeNode* fadeNode) -> RE::BSVisit::BSVisitControl {
			const bool isRoot = (fadeNode == root);

			auto fadeNodeName = std::format("{}.{}", name, fadeNode->name.c_str());
			CreateModelInternal(actor, isRoot ? name.c_str() : fadeNodeName.c_str(), fadeNode);

			return RE::BSVisit::BSVisitControl::kContinue;
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

	if (!cell->IsExteriorCell())
		return;

	auto& runtimeData = cell->GetRuntimeData();

	auto* exteriorData = runtimeData.cellData.exterior;

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
	if (!Scene::GetSingleton()->m_Settings.AdvancedSettings.EnableWater)
		return;

	if (!water || !object)
		return;

	if (m_WaterInstances.contains(object))
		return;

	auto path = std::format("Water_0x{:08X}", reinterpret_cast<uintptr_t>(object));

	logger::debug("SceneGraph::CreateWaterModel - FormID 0x{:08X}, {}", water->GetFormID(), path.c_str());

	// Creates all meshes, one for each valid BSGeometry found in the NiAVObject hierarchy
	auto meshes = CreateMeshes(water, object);

	if (auto* model = CommitModel(path.c_str(), object, water, meshes)) {
		if (auto* instance = AddInstanceImpl(object, model, 0))
			m_WaterInstances.emplace(object, instance);
	}
}

void SceneGraph::CreateLODModel(RE::BGSTerrainBlock* chunk)
{
	if (!m_TerrainLODInstances.contains(chunk))
		CreateLODModelImpl(chunk);
}

void SceneGraph::CreateLODModel(RE::BGSObjectBlock* chunk)
{
	if (!m_ObjectLODInstances.contains(chunk))
		CreateLODModelImpl(chunk);
}

template <typename T>
void SceneGraph::CreateLODModelImpl(T* block)
{
	auto node = block->chunk;

	if (!node)
		return;

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

	logger::debug("SceneGraph::CreateLODModel - {}, {}", node->name.c_str(), Util::Math::Float3(node->world.translate));

	auto rootWorldInverse = node->world.Invert();

	Util::Traversal::ScenegraphRTGeometries(node, nullptr, [&](RE::BSGeometry* pGeometry)->RE::BSVisit::BSVisitControl {
		if (pGeometry->GetType().none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kSubIndexTriShape))
			return RE::BSVisit::BSVisitControl::kContinue;

		logger::debug("\t{}, {}", pGeometry->name.c_str(), Util::Math::Float3(pGeometry->world.translate));

		const auto& geometryRuntimeData = pGeometry->GetGeometryRuntimeData();

		auto* triShapeRD = geometryRuntimeData.rendererData;

		if (!triShapeRD)
			return RE::BSVisit::BSVisitControl::kContinue;

		eastl::vector<eastl::unique_ptr<Mesh>> meshes;

		float3x4 localToRoot;
		XMStoreFloat3x4(&localToRoot, Util::Math::GetXMFromNiTransform(rootWorldInverse * pGeometry->world));

		auto triShape = netimmerse_cast<RE::BSTriShape*>(pGeometry);

		const auto& triShapeRuntime = triShape->GetTrishapeRuntimeData();

		const char* name = pGeometry->name.c_str();

		if (pGeometry->GetType().all(RE::BSGeometry::Type::kSubIndexTriShape)) {
			/*auto* subIndexTriShape = netimmerse_cast<RE::BSSubIndexTriShape*>(pGeometry);

			for (size_t i = 0; i < subIndexTriShape->numSegments; i++)
			{
				auto& segment = subIndexTriShape->segmentData[i];

				auto startIndex = segment.index;
				auto numTriangles = segment.numTris;

				const char* name = pGeometry->name.c_str();

				auto mesh = eastl::make_unique<Mesh>(RE::FormType::None, Mesh::Flags::ObjectLOD, name, pGeometry, localToRoot);

				mesh->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
				mesh->BuildMaterial(geometryRuntimeData, nullptr);

				meshes.push_back(eastl::move(mesh));
			}*/

			auto mesh = eastl::make_unique<Mesh>(RE::FormType::None, Mesh::Flags::LOD, name, pGeometry, localToRoot);

			mesh->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
			mesh->BuildMaterial(geometryRuntimeData, nullptr);

			meshes.push_back(eastl::move(mesh));
		}
		else {
			auto mesh = eastl::make_unique<Mesh>(RE::FormType::None, Mesh::Flags::LOD, name, pGeometry, localToRoot);

			mesh->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
			mesh->BuildMaterial(geometryRuntimeData, nullptr);

			meshes.push_back(eastl::move(mesh));
		}

		auto path = std::format("{}_0x{:08X}", pGeometry->name.c_str(), reinterpret_cast<uintptr_t>(pGeometry));

		if (auto* model = CommitModel(path.c_str(), pGeometry, nullptr, meshes))
			AddInstance(block, pGeometry, model);

		return RE::BSVisit::BSVisitControl::kContinue;
		});
}

template void SceneGraph::CreateLODModelImpl(RE::BGSTerrainBlock*);
template void SceneGraph::CreateLODModelImpl(RE::BGSObjectBlock*);

void SceneGraph::ActorEquip(RE::Actor* a_actor, RE::TESForm* a_form, RE::NiAVObject* a_object, eastl::vector<Mesh*>& a_meshes, bool firstPerson)
{
	if (!a_form)
		return;

	if (!a_object)
		return;

	auto it = m_InstancesFormIDs.find(a_actor->GetFormID());

	if (it == m_InstancesFormIDs.end())
		return;

	auto meshes = CreateMeshes(a_form, a_object);

	for (const auto& mesh: meshes)
		a_meshes.push_back(mesh.get());

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

	for (const auto& instance : it->second) {
		if (instance->model->m_FirstPerson == firstPerson) {
			instance->model->AppendMeshes(this, meshes);
			break;
		}
	}
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
	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);
	m_TextureManager->ReleaseTexture(texture);
}

void SceneGraph::ReleaseWaterInstance(RE::NiAVObject* node)
{
	auto it = m_WaterInstances.find(node);
	if (it == m_WaterInstances.end())
		return;

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);
	std::unique_lock releaseLock(m_ReleaseDataMutex);

	auto* instance = it->second;
	auto* model = instance->model;

	if (instance->model) {
		auto refCount = instance->model->Release();

		if (refCount <= 0) {
			auto modelIt = m_Models.find(model->m_Name);

			if (modelIt != m_Models.end()) {
				auto renderer = Renderer::GetSingleton();

				// Add to safe-release vector
				m_ReleasedData.emplace_back(renderer->GetFrameIndex(), eastl::move(modelIt->second));

				// Erase from list
				m_Models.erase(modelIt);
			}
		}

		instance->model = nullptr;
	}

	m_WaterInstances.erase(it);

	// Removes the original instance, all pointers past this point are invalid
	auto instIt = eastl::find_if(
		m_Instances.begin(),
		m_Instances.end(),
		[instance](auto& x) { return x.get() == instance; });

	if (instIt != m_Instances.end())
		m_Instances.erase(instIt);
}

void SceneGraph::ReleaseInstances(eastl::vector<Instance*>& instances, bool releaseModel)
{
	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);
	std::unique_lock releaseLock(m_ReleaseDataMutex);

	auto renderer = Renderer::GetSingleton();

	for (auto* instance : instances) {
		auto* model = instance->model;

		if (model) {
			auto refCount = model->Release();
			instance->model = nullptr;

			if (refCount <= 0 && releaseModel) {
				auto modelIt = m_Models.find(model->m_Name);

				if (modelIt != m_Models.end()) {
					m_ReleasedData.emplace_back(renderer->GetFrameIndex(), eastl::move(modelIt->second));
					m_Models.erase(modelIt);
				}
			}
		}

		auto instIt = eastl::find_if(
			m_Instances.begin(),
			m_Instances.end(),
			[instance](auto& x) { return x.get() == instance; });

		if (instIt != m_Instances.end())
			m_Instances.erase(instIt);
	}
}

void SceneGraph::ReleaseInstances(eastl::vector<Instance*>& instances)
{
	auto renderer = Renderer::GetSingleton();

	for (auto* instance : instances) {
		auto* model = instance->model;

		if (model) {
			auto refCount = model->Release();
			instance->model = nullptr;

			if (refCount <= 0) {
				auto modelIt = m_Models.find(model->m_Name);

				if (modelIt != m_Models.end()) {
					m_ReleasedData.emplace_back(renderer->GetFrameIndex(), eastl::move(modelIt->second));
					m_Models.erase(modelIt);
				}
			}

		}

		auto instIt = eastl::find_if(
			m_Instances.begin(),
			m_Instances.end(),
			[instance](auto& x) { return x.get() == instance; });

		if (instIt != m_Instances.end())
			m_Instances.erase(instIt);
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
	auto it = m_TerrainLODInstances.find(block);
	if (it == m_TerrainLODInstances.end())
		return;

	ReleaseInstances(it->second.instances, true);

	m_TerrainLODInstances.erase(it);
}

void SceneGraph::ReleaseInstances(RE::BGSObjectBlock* block)
{
	auto it = m_ObjectLODInstances.find(block);
	if (it == m_ObjectLODInstances.end())
		return;

	logger::info("SceneGraph::ReleaseInstances - Releasing {} instances for object block 0x{:08X}", it->second.instances.size(), reinterpret_cast<uintptr_t>(block));

	ReleaseInstances(it->second.instances, true);

	m_ObjectLODInstances.erase(it);
}

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

eastl::vector<eastl::unique_ptr<Mesh>> SceneGraph::CreateMeshes(RE::TESForm* form, RE::NiAVObject* object)
{
	auto formType = form->GetFormType();
	auto baseFormType = formType;

	if (auto* refr = form->AsReference()) {
		if (auto* baseObject = refr->GetBaseObject())
			baseFormType = baseObject->GetFormType();
	}

	auto rootWorldInverse = object->world.Invert();

	const bool isRootOrigin = object->world.translate == RE::NiPoint3::Zero();

	eastl::vector<eastl::unique_ptr<Mesh>> meshes;

	Util::Traversal::ScenegraphRTGeometries(object, nullptr, [&](RE::BSGeometry* pGeometry)->RE::BSVisit::BSVisitControl {
		const char* name = pGeometry->name.c_str();

		logger::trace("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - {}", name);

		const auto& geometryType = pGeometry->GetType();

		if (geometryType.none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kDynamicTriShape)) {
			logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Unsupported Geometry: {} for {}", magic_enum::enum_name(geometryType.get()), name);
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		const auto& geometryRuntimeData = pGeometry->GetGeometryRuntimeData();

		auto* shaderProperty = geometryRuntimeData.shaderProperty.get();

		if (!shaderProperty) {
			logger::debug("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - No Effect");
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		bool isLightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProperty) != nullptr;
		bool isEffectShader = netimmerse_cast<RE::BSEffectShaderProperty*>(shaderProperty) != nullptr;
		bool isWaterShader = netimmerse_cast<RE::BSWaterShaderProperty*>(shaderProperty) != nullptr;

		// Only lighting and effect shader for now
		if (!isLightingShader && !isEffectShader && !isWaterShader) {
			logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Unsupported shader type: {}", shaderProperty->GetRTTI()->name);
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		bool skinned = shaderProperty && shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSkinned);

		auto& geomFlags = pGeometry->GetFlags();

		if (geomFlags.any(RE::NiAVObject::Flag::kHidden) && !skinned) {
			logger::debug("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Is Hidden");
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		auto flags = Mesh::Flags::None;

		// Landscape needs special handling of triangles
		if (baseFormType == RE::FormType::Land)
			flags |= Mesh::Flags::Landscape;
		else if (baseFormType == RE::FormType::Water)
			flags |= Mesh::Flags::Water;

		if (geometryType.all(RE::BSGeometry::Type::kDynamicTriShape))
			flags |= Mesh::Flags::Dynamic;

		auto localToRoot = float3x4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f);

		if (auto* triShapeRD = geometryRuntimeData.rendererData) {  // Non-Skinned
			auto* pTriShape = netimmerse_cast<RE::BSTriShape*>(pGeometry);

			const auto& triShapeRuntime = pTriShape->GetTrishapeRuntimeData();

			if (triShapeRuntime.vertexCount == 0) {
				logger::error("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Vertex count of 0 for {}", name ? name : "N/A");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			if (triShapeRuntime.triangleCount == 0) {
				logger::error("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Triangle count of 0 for {}", name ? name : "N/A");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			const bool isOrigin = pGeometry->world.translate == RE::NiPoint3::Zero();

			// Some plants have parts with geometry world position of [0, 0, 0]
			// But so does some architecture (like Winterhold Arcanaeum) and they might depend on transformation for pivoted geometry
			if (!isOrigin || isOrigin && isRootOrigin)
				XMStoreFloat3x4(&localToRoot, Util::Math::GetXMFromNiTransform(rootWorldInverse * pGeometry->world));
			else
				flags |= Mesh::Flags::Origin;

			auto mesh = eastl::make_unique<Mesh>(baseFormType, flags, name, pGeometry, localToRoot);

			mesh->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
			mesh->BuildMaterial(geometryRuntimeData, form);

			meshes.push_back(eastl::move(mesh));
		}
		else if (auto* skinInstance = geometryRuntimeData.skinInstance.get()) {  // Skinned
			auto& skinPartition = skinInstance->skinPartition;

			if (!skinPartition) {
				logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Invalid SkinPartition");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			if (skinPartition->vertexCount == 0) {
				logger::error("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Vertex count of 0 for {}", name ? name : "N/A");
				return RE::BSVisit::BSVisitControl::kContinue;
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

				auto mesh = eastl::make_unique<Mesh>(baseFormType, flags, name, pGeometry, localToRoot, i);

				mesh->BuildMesh(partition.buffData, skinPartition->vertexCount, partition.triangles, partition.bonesPerVertex);
				mesh->BuildMaterial(geometryRuntimeData, form);

				meshes.push_back(eastl::move(mesh));
			}
		}

		return RE::BSVisit::BSVisitControl::kContinue;
	});

	return meshes;
}

uint32_t SceneGraph::CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* pRoot)
{
	if (!pRoot)
		return 0;

	if (!path || strlen(path) == 0)
		return 0;

	auto formID = form->GetFormID();

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

	// We only need one buffer per model
	if (auto it = m_Models.find(path); it != m_Models.end()) {
		AddInstance(formID, pRoot, path);
		return static_cast<uint32_t>(it->second->meshes.size());
	}

	logger::trace("SceneGraph::CreateModelInternal \"{}\"", typeid(*pRoot).name());

	const auto* bsxFlags = pRoot->GetExtraData<RE::BSXFlags>("BSX");

	if (bsxFlags) {
		if (static_cast<int32_t>(bsxFlags->value) & static_cast<int32_t>(RE::BSXFlags::Flag::kEditorMarker))
			return 0;
	}

	logger::debug("SceneGraph::CreateModelInternal - Path: {}, FormID [0x{:08X}], NiNode [0x{:08X}]: {}", path, formID, reinterpret_cast<uintptr_t>(pRoot), pRoot->name);

	// Creates all meshes, one for each valid BSGeometry found in the NiAVObject hierarchy
	auto meshes = CreateMeshes(form, pRoot);

	auto numMeshes = static_cast<uint32_t>(meshes.size());

	if (auto* model = CommitModel(path, pRoot, form, meshes))
		AddInstance(form->GetFormID(), pRoot, model);

	return numMeshes;
}

Model* SceneGraph::CommitModel(const char* path, RE::NiAVObject* object, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes) {
	if (auto shapeCount = meshes.size(); shapeCount > 0) {
		auto model = eastl::make_unique<Model>(path, object, form, meshes);

		auto& modelName = model->m_Name;

		auto [it, emplaced] = m_Models.try_emplace(modelName, eastl::move(model));

		if (emplaced) {
			auto* modelPtr = it->second.get();

			// Copy Command
			auto copyCommandList = Renderer::GetSingleton()->GetCopyCommandList();
			copyCommandList->open();

			modelPtr->CreateBuffers(this, copyCommandList);

			copyCommandList->close();

			auto device = Renderer::GetSingleton()->GetDevice();

			auto copySubmittedInstance = device->executeCommandList(copyCommandList, nvrhi::CommandQueue::Copy);

			// Compute Command
			auto computeCommandList = Renderer::GetSingleton()->GetComputeCommandList();
			computeCommandList->open();

			modelPtr->BuildBLAS(computeCommandList);

			computeCommandList->compactBottomLevelAccelStructs();

			computeCommandList->close();

			device->queueWaitForCommandList(nvrhi::CommandQueue::Compute, nvrhi::CommandQueue::Copy, copySubmittedInstance);

			auto computeSubmittedInstance = device->executeCommandList(computeCommandList, nvrhi::CommandQueue::Compute);

			// MSN Conversion - must happen after buffers are uploaded and GPU is idle
			if (modelPtr->ShouldQueueMSNConversion()) {
				auto graphicsCommandList = Renderer::GetSingleton()->GetGraphicsCommandList();
				graphicsCommandList->open();

				m_TextureManager->m_MSNConverter->Convert(modelPtr, graphicsCommandList, this);

				graphicsCommandList->close();

				device->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Compute, computeSubmittedInstance);

				device->executeCommandList(graphicsCommandList, nvrhi::CommandQueue::Graphics);
			}

			device->waitForIdle();

			logger::debug("SceneGraph::CreateModelInternal - Commited {} TriShapes to [0x{:08X}]", shapeCount, reinterpret_cast<uintptr_t>(modelPtr));

			return modelPtr;
		}
		else {
			logger::warn("SceneGraph::CreateModelInternal - Emplace failed for {} TriShapes", shapeCount);
		}
	}
	else {
		logger::debug("SceneGraph::CreateModelInternal - No TriShapes to commit");
	}

	return nullptr;
}

Instance* SceneGraph::AddInstanceImpl(RE::NiAVObject* node, Model* model, RE::FormID formID)
{
	auto& instance = m_Instances.emplace_back(eastl::make_unique<Instance>(formID, node, model));
	instance->model->AddRef();

	return instance.get();
}

void SceneGraph::AddInstance(RE::FormID formID, RE::NiAVObject* node, eastl::string path)
{
	auto modelIt = m_Models.find(path);
	if (modelIt == m_Models.end())
		return;

	if (auto* instance = AddInstanceImpl(node, modelIt->second.get(), formID))
		m_InstancesFormIDs[formID].push_back(instance);
}

void SceneGraph::AddInstance(RE::FormID formID, RE::NiAVObject* node, Model* model)
{
	if (auto* instance = AddInstanceImpl(node, model, formID))
		m_InstancesFormIDs[formID].push_back(instance);
}

void SceneGraph::AddInstance(RE::BGSObjectBlock* block, RE::NiAVObject* node, Model* model)
{
	if (auto* instance = AddInstanceImpl(node, model, 0))
		m_ObjectLODInstances[block].instances.push_back(instance);
}

void SceneGraph::AddInstance(RE::BGSTerrainBlock* block, RE::NiAVObject* node, Model* model)
{
	if (auto* instance = AddInstanceImpl(node, model, 0))
		m_TerrainLODInstances[block].instances.push_back(instance);
}

void SceneGraph::SetLODDetached(RE::BGSTerrainBlock* block, bool detached)
{
	auto it = m_TerrainLODInstances.find(block);
	if (it == m_TerrainLODInstances.end())
		return;

	Scene::GetSingleton()->m_SceneMutex.lock();

	for (auto& instance : it->second.instances) {
		instance->SetDetached(detached);
	}

	Scene::GetSingleton()->m_SceneMutex.unlock();

	if (detached && detached != it->second.detached)
		it->second.detachedTime = std::chrono::steady_clock::now();

	it->second.detached = detached;
}

void SceneGraph::SetLODDetached(RE::BGSObjectBlock* block, bool detached)
{
	auto it = m_ObjectLODInstances.find(block);
	if (it == m_ObjectLODInstances.end())
		return;

	Scene::GetSingleton()->m_SceneMutex.lock();

	for (auto& instance : it->second.instances) {
		instance->SetDetached(detached);
	}

	Scene::GetSingleton()->m_SceneMutex.unlock();

	if (detached && detached != it->second.detached)
		it->second.detachedTime = std::chrono::steady_clock::now();

	it->second.detached = detached;
}

void SceneGraph::RunGarbageCollection(uint64_t frameIndex)
{
	m_ReleaseDataMutex.lock();

	// Clear LOD
	{
		using namespace std::chrono;
		const auto now = steady_clock::now();

		// Object LOD
		for (auto it = m_ObjectLODInstances.begin(); it != m_ObjectLODInstances.end(); ) {
			if (it->second.detached && now - it->second.detachedTime > LODBlockReference::maxDetachedTime) {
				ReleaseInstances(it->second.instances);
				it = m_ObjectLODInstances.erase(it);
			}
			else {
				++it;
			}
		}

		// Terrain LOD
		for (auto it = m_TerrainLODInstances.begin(); it != m_TerrainLODInstances.end(); ) {
			if (it->second.detached && now - it->second.detachedTime > LODBlockReference::maxDetachedTime) {
				ReleaseInstances(it->second.instances);
				it = m_TerrainLODInstances.erase(it);
			}
			else {
				++it;
			}
		}
	}

	// Clear Released Data
	{

		for (auto it = m_ReleasedData.begin(); it != m_ReleasedData.end(); ) {
			if (it->frameIndex < frameIndex - 1 && it->model->m_LastBLASUpdate < frameIndex - 1) {
				logger::debug("SceneGraph::RunGarbageCollection - Frame Index {}, Last Update {}, {}", it->frameIndex, it->model->m_LastBLASUpdate, it->model->m_Name);
				it = m_ReleasedData.erase(it);
			}
			else {
				++it;
			}
		}
	}

	m_ReleaseDataMutex.unlock();
}