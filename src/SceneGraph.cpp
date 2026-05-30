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

void SceneGraph::UpdateActors(nvrhi::ICommandList* commandList)
{
	for (auto& [formID, actorRef]: m_Actors)
	{
		actorRef.Update(commandList);
	}
}

void SceneGraph::UpdateLODVisibility()
{
	for (auto& [block, ref] : m_TerrainLODInstances)
	{
		ref.UpdateVisibility();
	}

	for (auto& [block, ref] : m_ObjectLODInstances)
	{
		ref.UpdateVisibility();
	}

	for (auto& [block, ref] : m_TreeLODInstances)
	{
		ref.UpdateVisibility();
	}
}

void SceneGraph::Update(nvrhi::ICommandList* commandList)
{
	UpdateLights(commandList);

	m_NumMeshes = 0;
	m_NumInstances = 0;

	eastl::array<uint8_t, Constants::INSTANCE_LIGHTS_MAX> lights;

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
			instance->GetAlpha()
		};

		m_NumInstances++;
		return Iterator::Continue;
	});

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
			auto* instance = AddInstance(actor->GetFormID(), object, model);
			m_Actors.try_emplace(
				actor->GetFormID(), 
				actor, firstPerson, instance, faceMeshes, bipedMeshes);
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

bool SceneGraph::CreateLODModel(RE::BGSTerrainBlock* chunk)
{
	if (!m_TerrainLODInstances.contains(chunk)) {
		CreateLODModelImpl(chunk, Mesh::Type::LandLOD);
		return false;
	}

	return true;
}

bool SceneGraph::CreateLODModel(RE::BGSObjectBlock* chunk)
{
	if (!m_ObjectLODInstances.contains(chunk)) {
		CreateLODModelImpl(chunk, Mesh::Type::ObjectLOD);
		return false;
	}

	return true;
}

bool SceneGraph::CreateLODModel(RE::BGSDistantTreeBlock* block)
{
	if (m_TreeLODInstances.contains(block))
		return true;

	for (const auto& group: block->treeGroups)
	{
		if (!group->geometry)
			continue;

		auto* geometry = group->geometry.get();

		auto modelNameTmp = std::format("TreeLOD_{}", group->treeType);
		auto modelName = eastl::string(modelNameTmp.c_str());

		Model* model = nullptr;
		{
			std::scoped_lock lock(m_ModelMutex);
			if (auto it = m_Models.find(modelName); it != m_Models.end())
				model = it->second.get();
		}

		if (!model) {
			auto meshes = CreateMeshes(geometry, nullptr);
			model = CommitModel(modelName.c_str(), geometry, nullptr, meshes);
		}

		if (!model)
			logger::warn("SceneGraph::CreateLODModel - Tree lod model {} is null", group->treeType);

		auto& blockRefr = m_TreeLODInstances[block];
		blockRefr.block = block;

		for (auto& instanceData: group->instances)
		{
			auto* instanceDataPtr = &instanceData;

			auto instance = eastl::make_unique<TreeLODInstance>(instanceDataPtr, geometry, model);
			instance->model->AddRef();

			blockRefr.instances.push_back(instance.get());
			blockRefr.treeInstanceData.push_back(instanceDataPtr);

			m_Instances.Add(eastl::move(instance));
		}
	}

	return false;
}

template <typename T>
void SceneGraph::CreateLODModelImpl(T* block, Mesh::Type type)
{
	auto node = block->chunk;

	if (!node)
		return;

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
			auto* subIndexTriShape = netimmerse_cast<RE::BSSubIndexTriShape*>(pGeometry);

			if (subIndexTriShape) {
				stl::enumeration<Mesh::Flags> flags = Mesh::Flags::LOD;
				auto vertexData = Mesh::BuildVertices(flags, pGeometry, triShapeRD, triShapeRuntime.vertexCount, 0);
				auto triangleData = Mesh::BuildTriangles(flags.get(), triShapeRD, triShapeRuntime.triangleCount);

				auto& runtimeData = subIndexTriShape->GetSubIndexedTrishapeRuntimeData();
				
				logger::debug("SubIndexTriShape - Triangles: {}", triShapeRuntime.triangleCount);

				logger::debug("SubIndexTriShape - Segments: {}, UnkSegments: {}, Unk170: {}, NonSegmented: {}",
					runtimeData.numSegments, runtimeData.unkSegCount, runtimeData.unk170, runtimeData.nonSegmented);

				for (size_t i = 0; i < runtimeData.numSegments; i++)
				{
					// The first segment contains all triangles (it is the equivalent of all other segments combine)
					if (i == 0 && runtimeData.numSegments > 1)
						continue;

					auto& segment = runtimeData.segmentData[i];

					// Invalid segment
					if (segment.unkTriCount == 0)
						continue;

					logger::debug("\tSegment[{}]: Index: {}, UnkTriCount: {}, UnkFlags: 0x{:08X}, NumTris: {}, Flags: 0x{:08X}",
						i, segment.index, segment.unkTriCount, segment.unkFlags, segment.numTris, segment.flags);

					auto mesh = eastl::make_unique<Mesh>(RE::FormType::None, type, flags.get(), name, pGeometry, localToRoot, i);

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
			}
		}
		else {
			auto mesh = eastl::make_unique<Mesh>(RE::FormType::None, type, Mesh::Flags::LOD, name, pGeometry, localToRoot);

			mesh->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
			mesh->BuildMaterial(geometryRuntimeData, 0);

			meshes.push_back(eastl::move(mesh));
		}

		auto path = std::format("{}_0x{:08X}", pGeometry->name.c_str(), reinterpret_cast<uintptr_t>(pGeometry));

		if (auto* model = CommitModel(path.c_str(), pGeometry, nullptr, meshes))
			AddInstance(block, pGeometry, model);

		return RE::BSVisit::BSVisitControl::kContinue;
	});
}

void SceneGraph::ActorEquip(nvrhi::ICommandList* commandList, Instance* a_instance, RE::TESForm* a_form, RE::NiAVObject* a_object, eastl::vector<Mesh*>& a_meshes, bool firstPerson)
{
	if (!a_form)
		return;

	if (!a_object)
		return;

	auto meshes = CreateMeshes(a_object, a_form);

	for (const auto& mesh: meshes)
		a_meshes.push_back(mesh.get());

	if (a_instance->model->m_FirstPerson != firstPerson)
		return;

	a_instance->model->AppendMeshes(this, commandList, meshes);
}

void SceneGraph::ActorUnequip(Instance* a_instance, const eastl::vector<Mesh*>& a_meshes, bool firstPerson)
{
	if (a_instance->model->m_FirstPerson != firstPerson)
		return;

	a_instance->model->RemoveMeshes(a_meshes);
}

void SceneGraph::ReleaseTexture(RE::BSGraphics::Texture* texture)
{
	m_TextureManager->ReleaseTexture(texture);
}

void SceneGraph::ReleaseModel(const Model* model)
{
	std::scoped_lock modelLock(m_ModelMutex);

	auto it = m_Models.find(model->m_Name);
	if (!model->m_BuffersUploaded)
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

void SceneGraph::ReleaseInstances(eastl::vector<Instance*>& instances)
{
	for (auto* instance : instances) {
		m_Instances.Remove(InstanceManager::RemoveParams(instance, true));
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

eastl::vector<eastl::unique_ptr<Mesh>> SceneGraph::CreateMeshes(RE::NiAVObject* object, RE::TESForm* form)
{
	auto formType = form ? form->GetFormType() : RE::FormType::None;
	auto baseFormType = formType;

	if (form) {
		if (auto* refr = form->AsReference()) {
			if (auto* baseObject = refr->GetBaseObject())
				baseFormType = baseObject->GetFormType();
		}
	}

	auto rootWorldInverse = object->world.Invert();

	const bool isRootOrigin = object->world.translate == RE::NiPoint3::Zero();

	eastl::vector<eastl::unique_ptr<Mesh>> meshes;

	if (object->HasExtraData("HDT Skinned Mesh Physics Object"))
		return meshes;

	Util::Traversal::ScenegraphRTGeometries(object, nullptr, [&](RE::BSGeometry* pGeometry)->RE::BSVisit::BSVisitControl {
		const char* name = pGeometry->name.c_str();

		if (Util::Geometry::IsBlocklisted(name))
			return RE::BSVisit::BSVisitControl::kContinue;

		logger::trace("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - {}", name);

		const auto& geometryType = pGeometry->GetType();

		if (geometryType.none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kDynamicTriShape, RE::BSGeometry::Type::kMultiStreamInstanceTriShape)) {
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
		bool isTreeLODShader = netimmerse_cast<RE::BSDistantTreeShaderProperty*>(shaderProperty) != nullptr;
		bool isGrassShader = netimmerse_cast<RE::BSGrassShaderProperty*>(shaderProperty) != nullptr;

		// Only lighting and effect shader for now
		if (!isLightingShader && !isEffectShader && !isWaterShader && !isTreeLODShader && !isGrassShader) {
			logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Unsupported shader type: {}", shaderProperty->GetRTTI()->name);
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		// Ignore effect shader with alpha blend
		if (isEffectShader && geometryRuntimeData.alphaProperty)
			if (geometryRuntimeData.alphaProperty->GetAlphaBlending())
				return RE::BSVisit::BSVisitControl::kContinue;

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

		const bool isOrigin = pGeometry->world.translate == RE::NiPoint3::Zero();

		// Some plants have parts with geometry world position of [0, 0, 0]
		// But so does some architecture (like Winterhold Arcanaeum) and they might depend on transformation for pivoted geometry
		if (!isOrigin || isOrigin && isRootOrigin)
			XMStoreFloat3x4(&localToRoot, Util::Math::GetXMFromNiTransform(rootWorldInverse * pGeometry->world));
		else
			flags |= Mesh::Flags::Origin;

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

			auto mesh = eastl::make_unique<Mesh>(baseFormType, Mesh::Type::Default, flags, name, pGeometry, localToRoot);

			mesh->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
			mesh->BuildMaterial(geometryRuntimeData, form ? form->formID : 0);

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

				auto mesh = eastl::make_unique<Mesh>(baseFormType, Mesh::Type::Default, flags, name, pGeometry, localToRoot, i);

				mesh->BuildMesh(partition.buffData, skinPartition->vertexCount, partition.triangles, partition.bonesPerVertex);
				mesh->BuildMaterial(geometryRuntimeData, form ? form->formID : 0);

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

	Model* model = nullptr;
	{
		std::scoped_lock lock(m_ModelMutex);

		// We only need one buffer per model
		if (auto it = m_Models.find(path); it != m_Models.end())
			model = it->second.get();
	}

	if (model) {
		AddInstance(formID, pRoot, model);
		return static_cast<uint32_t>(model->m_Meshes.size());
	}

	logger::trace("SceneGraph::CreateModelInternal \"{}\"", typeid(*pRoot).name());

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

		auto model = eastl::make_unique<Model>(path, object, form, meshes);
		auto* modelPtr = model.get();

		m_ModelMutex.lock();
		auto [it, emplaced] = m_Models.try_emplace(model->m_Name, eastl::move(model));
		m_ModelMutex.unlock();

		if (emplaced) {
			// Copy Command
			modelPtr->CreateBuffers(this);

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
			logger::warn("SceneGraph::CommitModel - Emplace failed for {} TriShapes", shapeCount);
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

Instance* SceneGraph::AddInstance(RE::FormID formID, RE::NiAVObject* node, Model* model)
{
	if (auto* instance = AddInstanceImpl(node, model, formID)) {
		m_InstancesFormIDs[formID].push_back(instance);
		return instance;
	}

	return nullptr;
}

void SceneGraph::AddInstance(RE::BGSObjectBlock* block, RE::NiAVObject* node, Model* model)
{
	if (auto* instance = AddInstanceImpl(node, model, 0)) {
		auto& blockRefr = m_ObjectLODInstances[block];
		blockRefr.block = block;
		blockRefr.instances.push_back(instance);
	}
}

void SceneGraph::AddInstance(RE::BGSTerrainBlock* block, RE::NiAVObject* node, Model* model)
{
	if (auto* instance = AddInstanceImpl(node, model, 0)) {
		auto& blockRefr = m_TerrainLODInstances[block];
		blockRefr.block = block;
		blockRefr.instances.push_back(instance);
	}
}

void SceneGraph::SetLODDetached(RE::BGSTerrainBlock* block, bool detached)
{
	auto it = m_TerrainLODInstances.find(block);
	if (it == m_TerrainLODInstances.end())
		return;

	for (auto& instance : it->second.instances) {
		instance->SetDetached(detached);
	}

	if (detached && detached != it->second.detached)
		it->second.detachedTime = std::chrono::steady_clock::now();

	it->second.detached = detached;
}

void SceneGraph::SetLODDetached(RE::BGSObjectBlock* block, bool detached)
{
	auto it = m_ObjectLODInstances.find(block);
	if (it == m_ObjectLODInstances.end())
		return;

	for (auto& instance : it->second.instances) {
		instance->SetDetached(detached);
	}

	if (detached && detached != it->second.detached)
		it->second.detachedTime = std::chrono::steady_clock::now();

	it->second.detached = detached;
}

void SceneGraph::SetLODDetached(RE::BGSDistantTreeBlock* block, bool detached)
{
	auto it = m_TreeLODInstances.find(block);
	if (it == m_TreeLODInstances.end())
		return;

	for (auto& instance : it->second.instances) {
		instance->SetDetached(detached);
	}

	if (detached && detached != it->second.detached)
		it->second.detachedTime = std::chrono::steady_clock::now();

	it->second.detached = detached;
}

void SceneGraph::RunGarbageCollection()
{
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

		// Tree LOD
		for (auto it = m_TreeLODInstances.begin(); it != m_TreeLODInstances.end(); ) {
			if (it->second.detached && now - it->second.detachedTime > LODBlockReference::maxDetachedTime) {
				ReleaseInstances(it->second.instances);
				it = m_TreeLODInstances.erase(it);
			}
			else {
				++it;
			}
		}
	}

	// Clear Models
	{
		std::scoped_lock modelLock(m_ModelReleaseMutex);

		for (auto it = m_ReleasedModels.begin(); it != m_ReleasedModels.end(); ) {
			auto* model = it->get();

			model->UpdateFlags();

			if (model->m_BuffersUploaded)
				it = m_ReleasedModels.erase(it);
			else
				++it;
		}		
	}
}