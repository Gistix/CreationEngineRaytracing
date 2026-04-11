#include "SceneGraph.h"

#include "Scene.h"

#include "core/Mesh.h"

#include "Renderer.h"
#include "Util.h"
#include "ShaderUtils.h"

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

	// Texture bindless descriptor table
	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = Constants::NUM_TEXTURES_MAX;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::Texture_SRV(3).setSize(UINT_MAX)
		};

		m_TextureDescriptors = eastl::make_unique<BindlessTableManager>(device, bindlessLayoutDesc, true);
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
}

void SceneGraph::UpdateLights(nvrhi::ICommandList* commandList)
{
	//auto& mainSSNRuntimeData = RE::DrawWorld::GetSingleton().mainShadowSceneNode->GetRuntimeData();
	auto& mainSSNRuntimeData = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]->GetRuntimeData();

	//auto accumulator = *m_CurrentAccumulator.get();
	//auto& mainSSNRuntimeData = accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData();

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

void SceneGraph::Update(nvrhi::ICommandList* commandList)
{
	UpdateLights(commandList);

	uint32_t meshIndex = 0;
	uint32_t instanceIndex = 0;

	eastl::array<uint8_t, Constants::INSTANCE_LIGHTS_MAX> lights;

	for (auto& instance : m_Instances)
	{
		instance->Update(instanceIndex);

		if (instance->IsHidden())
			continue;

		bool isPlayer = Util::IsPlayerFormID(instance->m_FormID);

		// Update if applicabe and queue skinning/dynamic update
		instance->model->Update(instance->m_Node, isPlayer);

		uint32_t firstMeshIndex = meshIndex;

		// Get mesh data
		instance->model->SetData(m_MeshData.data(), meshIndex);
		
		// No visible shape in instance
		if (meshIndex == firstMeshIndex)
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

		m_InstanceData[instanceIndex] = {
			instance->m_Transform,
			instance->m_PrevTransform,
			InstanceLightData(lights.data(), numLights),
			firstMeshIndex
		};

		instanceIndex++;
	}

	if (meshIndex > 0)
		commandList->writeBuffer(m_MeshBuffer, m_MeshData.data(), meshIndex * sizeof(MeshData));

	if (instanceIndex > 0)
		commandList->writeBuffer(m_InstanceBuffer, m_InstanceData.data(), instanceIndex * sizeof(InstanceData));
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

	const REL::Relocation<const RE::NiRTTI*> rtti{ RE::NiMultiTargetTransformController::Ni_RTTI };
	auto* controller = reinterpret_cast<RE::NiMultiTargetTransformController*>(root->GetController(rtti.get()));

	if (controller) {
		eastl::hash_set<RE::NiNode*> parents;
		eastl::hash_set<RE::NiAVObject*> targets;

		for (uint16_t i = 0; i < controller->numInterps; i++) {
			auto* target = controller->targets[i];

			if (!target)
				continue;

			auto [it, emplaced] = targets.emplace(target);
			parents.emplace(target->parent);

			if (!emplaced)
				continue;

			CreateModelInternal(form, std::format("{}_{}", model, target->name.c_str()).c_str(), target);
		}

		for (auto* parent : parents) {
			for (auto& child : parent->GetChildren()) {
				if (targets.find(child.get()) != targets.end())
					continue;

				CreateModelInternal(form, std::format("{}_{}_{}", model, child->name.c_str(), child->parentIndex).c_str(), child.get());
			}
		}

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

		auto commited = CommitModel(name.c_str(), object, actor, meshes);

		if (commited) {
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

		if (!mesh)
			continue;

		CreateModelInternal(land, std::format("Landscape_{}_{}_Quad_{}", exteriorData->cellX, exteriorData->cellY, i).c_str(), mesh);
	}
}

void SceneGraph::CreateWaterModel(RE::TESWaterForm* water, RE::NiAVObject* object)
{
	if (!Scene::GetSingleton()->m_Settings.DebugSettings.EnableWater)
		return;

	if (!water)
		return;

	if (!object)
		return;

	auto path = std::format("Water_0x{:08X}", reinterpret_cast<uintptr_t>(object));

	logger::debug("SceneGraph::CreateWaterModel - FormID 0x{:08X}, {}", water->GetFormID(), path.c_str());

	CreateModelInternal(water, path.c_str(), object);
}

void SceneGraph::CreateLODModel(RE::NiNode* node)
{
	if (!node)
		return;
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

void SceneGraph::ReleaseTexture(ID3D11Texture2D* texture)
{
	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

	m_Textures.erase(texture);
}

void SceneGraph::ReleaseObjectInstance(RE::NiAVObject* node, bool releaseModel)
{
	auto instanceNodeIt = m_InstanceNodes.find(node);

	if (instanceNodeIt == m_InstanceNodes.end())
		return;

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);
	std::unique_lock releaseLock(m_ReleaseDataMutex);

	auto& instance = instanceNodeIt->second;

	auto refCount = instance->model->Release();

	if (refCount <= 0 && releaseModel) {
		logger::debug("SceneGraph::ReleaseObjectInstance - {}", instance->model->m_Name);

		auto modelIt = m_Models.find(instance->model->m_Name);

		if (modelIt != m_Models.end()) {
			auto renderer = Renderer::GetSingleton();

			// Add to safe-release vector
			m_ReleasedData.emplace_back(renderer->GetFrameIndex(), eastl::move(modelIt->second));

			// Erase from list
			m_Models.erase(modelIt);
		}
	}

	m_InstanceNodes.erase(instanceNodeIt);

	m_InstancesFormIDs.erase(instance->m_FormID);

	// Removes the original instance, all pointers past this point are invalid
	auto instIt = eastl::find_if(
		m_Instances.begin(),
		m_Instances.end(),
		[&](auto& x) { return x.get() == instance; });

	if (instIt != m_Instances.end())
		m_Instances.erase(instIt);
}

void SceneGraph::ReleaseFormInstances(RE::TESForm* form, bool releaseModel)
{
	auto formID = form->GetFormID();

	if (releaseModel) {
		m_Actors.erase(formID);
	}

	auto instanceFormIDsIt = m_InstancesFormIDs.find(formID);

	// No instance to remove
	if (instanceFormIDsIt == m_InstancesFormIDs.end())
		return;

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

	auto renderer = Renderer::GetSingleton();

	// A single form can hold multiple model instances
	for (auto& instance : instanceFormIDsIt->second) {
		m_InstanceNodes.erase(instance->m_Node);

		auto refCount = instance->model->Release();

		if (refCount <= 0 && releaseModel) {
			logger::debug("SceneGraph::ReleaseFormInstances - {}", instance->model->m_Name);

			auto modelIt = m_Models.find(instance->model->m_Name);

			if (modelIt != m_Models.end()) {
				// Add to safe-release vector
				m_ReleasedData.emplace_back(renderer->GetFrameIndex(), eastl::move(modelIt->second));

				// Erase from list
				m_Models.erase(modelIt);
			}
		}

		// Removes the original instance, all pointers past this point are invalid
		auto instIt = eastl::find_if(
			m_Instances.begin(),
			m_Instances.end(),
			[&](auto& x) { return x.get() == instance; });

		if (instIt != m_Instances.end())
			m_Instances.erase(instIt);
	}

	m_InstancesFormIDs.erase(instanceFormIDsIt);
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

eastl::shared_ptr<DescriptorHandle> SceneGraph::GetTextureDescriptor(ID3D11Resource* d3d11Resource)
{
	if (!d3d11Resource)
		return nullptr;

	auto d3d11Texture = reinterpret_cast<ID3D11Texture2D*>(d3d11Resource);

	if (auto refIt = m_Textures.find(d3d11Texture); refIt != m_Textures.end())
		return refIt->second->descriptorHandle;

	winrt::com_ptr<IDXGIResource> dxgiResource;
	HRESULT hr = d3d11Texture->QueryInterface(IID_PPV_ARGS(&dxgiResource));

	if (FAILED(hr)) {
		logger::error("SceneGraph::GetTextureRegister - Failed to query interface.");
		return nullptr;
	}

	HANDLE sharedHandle = nullptr;
	hr = dxgiResource->GetSharedHandle(&sharedHandle);

	if (FAILED(hr) || !sharedHandle) {
		D3D11_TEXTURE2D_DESC desc;
		d3d11Texture->GetDesc(&desc);

		logger::debug("SceneGraph::GetTextureRegister - Failed to get shared handle - [{}, {}] Format: {}", desc.Width, desc.Height, magic_enum::enum_name(desc.Format));
		return nullptr;
	}

	auto* d3d12Device = Renderer::GetSingleton()->GetNativeD3D12Device();

	winrt::com_ptr<ID3D12Resource> d3d12Texture;
	hr = d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(&d3d12Texture));

	if (FAILED(hr)) {
		logger::error("SceneGraph::GetTextureRegister - Failed to open shared handle.");
		return nullptr;
	}

	if (!d3d12Texture) {
		logger::error("[RT] GetTextureRegister - Failed to adquire DX12 texture.");
		return nullptr;
	}


	D3D12_RESOURCE_DESC nativeTexDesc = d3d12Texture->GetDesc();

	auto formatIt = Renderer::GetFormatMapping().find(nativeTexDesc.Format);

	if (formatIt == Renderer::GetFormatMapping().end()) {
		logger::error("SceneGraph::GetTextureRegister - Unmapped format {}", magic_enum::enum_name(nativeTexDesc.Format));
		return nullptr;
	}

	auto& textureDesc = nvrhi::TextureDesc()
		.setWidth(static_cast<uint32_t>(nativeTexDesc.Width))
		.setHeight(nativeTexDesc.Height)
		.setFormat(formatIt->second)
		.setInitialState(nvrhi::ResourceStates::ShaderResource)
		.setDebugName("Shared Texture [?]");

	auto textureHandle = Renderer::GetSingleton()->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(d3d12Texture.get()), textureDesc);

	auto [it, emplaced] = m_Textures.try_emplace(d3d11Texture, nullptr);

	if (emplaced) {
		it->second = eastl::make_unique<TextureReference>(textureHandle, m_TextureDescriptors->m_DescriptorTable.get());
		return it->second->descriptorHandle;
	}
	else
		logger::error("SceneGraph::GetTextureRegister - TextureReference emplace failed.");

	return nullptr;
}

eastl::shared_ptr<DescriptorHandle> SceneGraph::GetMSNormalMapDescriptor([[maybe_unused]] Mesh* mesh, RE::BSGraphics::Texture* texture)
{
	auto* d3d11Texture = reinterpret_cast<ID3D11Texture2D*>(texture->texture);

	if (auto refIt = m_NormalMaps.find(d3d11Texture); refIt != m_NormalMaps.end()) {
		return refIt->second->textureRef->descriptorHandle;
	}

	auto [it, emplaced] = m_NormalMaps.emplace(d3d11Texture, eastl::make_unique<ConvertedNormalMap>());

	if (!emplaced) {
		logger::warn("[RT] GetMSNormalMapDescriptor - NormalMap emplace failed.");
		return nullptr;
	}

	auto* normalMap = it->second.get();

	D3D11_TEXTURE2D_DESC desc;
	d3d11Texture->GetDesc(&desc);

	// Share the DX11 MSN source texture to DX12
	{
		winrt::com_ptr<IDXGIResource> dxgiResource;
		HRESULT hr = d3d11Texture->QueryInterface(IID_PPV_ARGS(&dxgiResource));

		if (FAILED(hr)) {
			logger::error("[RT] GetMSNormalMapDescriptor - Failed to query interface for source.");
			m_NormalMaps.erase(it);
			return nullptr;
		}

		HANDLE sharedHandle = nullptr;
		hr = dxgiResource->GetSharedHandle(&sharedHandle);

		if (FAILED(hr) || !sharedHandle) {
			logger::error("[RT] GetMSNormalMapDescriptor - Failed to get shared handle for source.");
			m_NormalMaps.erase(it);
			return nullptr;
		}

		auto* d3d12Device = Renderer::GetSingleton()->GetNativeD3D12Device();

		winrt::com_ptr<ID3D12Resource> d3d12Texture;
		hr = d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(&d3d12Texture));

		if (FAILED(hr) || !d3d12Texture) {
			logger::error("[RT] GetMSNormalMapDescriptor - Failed to open shared handle for source.");
			m_NormalMaps.erase(it);
			return nullptr;
		}

		D3D12_RESOURCE_DESC nativeTexDesc = d3d12Texture->GetDesc();
		auto formatIt = Renderer::GetFormatMapping().find(nativeTexDesc.Format);

		if (formatIt == Renderer::GetFormatMapping().end()) {
			logger::error("[RT] GetMSNormalMapDescriptor - Unmapped source format: {}", magic_enum::enum_name(nativeTexDesc.Format));
			m_NormalMaps.erase(it);
			return nullptr;
		}

		normalMap->sourceTexture = Renderer::GetSingleton()->GetDevice()->createHandleForNativeTexture(
			nvrhi::ObjectTypes::D3D12_Resource,
			nvrhi::Object(d3d12Texture.get()),
			nvrhi::TextureDesc()
				.setWidth(static_cast<uint32_t>(nativeTexDesc.Width))
				.setHeight(nativeTexDesc.Height)
				.setFormat(formatIt->second)
				.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
				.setDebugName("MSN Source Texture"));
	}

	// Create DX12 render target for the converted normal map
	{
		auto device = Renderer::GetSingleton()->GetDevice();

		normalMap->convertedTexture = device->createTexture(
			nvrhi::TextureDesc()
				.setWidth(desc.Width)
				.setHeight(desc.Height)
				.setFormat(nvrhi::Format::R10G10B10A2_UNORM)
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setIsRenderTarget(true)
				.setDebugName("Converted MSN Texture"));

		normalMap->textureRef = eastl::make_unique<TextureReference>(normalMap->convertedTexture, m_TextureDescriptors->m_DescriptorTable.get());

		m_MSNAllocationMap.emplace(normalMap->textureRef->descriptorHandle->Get(), d3d11Texture);
	}

	return normalMap->textureRef->descriptorHandle;
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

		auto* effect = geometryRuntimeData.properties[RE::BSGeometry::States::kEffect].get();

		if (!effect) {
			logger::debug("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - No Effect");
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		bool isLightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect) != nullptr;
		bool isEffectShader = netimmerse_cast<RE::BSEffectShaderProperty*>(effect) != nullptr;
		bool isWaterShader = netimmerse_cast<RE::BSWaterShaderProperty*>(effect) != nullptr;

		// Only lighting and effect shader for now
		if (!isLightingShader && !isEffectShader && !isWaterShader) {
			logger::warn("\t\tSceneGraph::CreateMeshes::TraverseScenegraphGeometries - Unsupported shader type: {}", effect->GetRTTI()->name);
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		auto shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(effect);
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

			auto mesh = eastl::make_unique<Mesh>(flags, name, pGeometry, localToRoot);

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

				auto mesh = eastl::make_unique<Mesh>(flags, name, pGeometry, localToRoot, i);

				mesh->BuildMesh(partition.buffData, skinPartition->vertexCount, partition.triangles, partition.bonesPerVertex);
				mesh->BuildMaterial(geometryRuntimeData, form);

				meshes.push_back(eastl::move(mesh));
			}
		}

		return RE::BSVisit::BSVisitControl::kContinue;
	});

	return meshes;
}

void SceneGraph::CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* pRoot)
{
	if (!pRoot)
		return;

	if (!path || strlen(path) == 0)
		return;

	if (m_InstanceNodes.find(pRoot) != m_InstanceNodes.end()) {
		logger::warn("SceneGraph::CreateModelInternal \"{}\" - Instance/Model for 0x{:08X} already present.", path, reinterpret_cast<uintptr_t>(pRoot));
		return;
	}
	
	auto formID = form->GetFormID();

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

	// We only need one buffer per model
	if (m_Models.find(path) != m_Models.end()) {
		AddInstance(formID, pRoot, path);
		return;
	}

	logger::trace("SceneGraph::CreateModelInternal \"{}\"", typeid(*pRoot).name());

	const auto* bsxFlags = pRoot->GetExtraData<RE::BSXFlags>("BSX");

	if (bsxFlags) {
		if (static_cast<int32_t>(bsxFlags->value) & static_cast<int32_t>(RE::BSXFlags::Flag::kEditorMarker))
			return;
	}

	logger::debug("SceneGraph::CreateModelInternal - Path: {}, FormID [0x{:08X}], NiNode [0x{:08X}]: {}", path, formID, reinterpret_cast<uintptr_t>(pRoot), pRoot->name);

	// Creates all meshes, one for each valid BSGeometry found in the NiAVObject hierarchy
	auto meshes = CreateMeshes(form, pRoot);

	CommitModel(path, pRoot, form, meshes);
}

bool SceneGraph::CommitModel(const char* path, RE::NiAVObject* object, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes) {
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
				auto graphicsCommandList = device->createCommandList(
					nvrhi::CommandListParameters().setQueueType(nvrhi::CommandQueue::Graphics));
				graphicsCommandList->open();

				ConvertMSN(modelPtr, graphicsCommandList);

				graphicsCommandList->close();

				device->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Compute, computeSubmittedInstance);

				device->executeCommandList(graphicsCommandList, nvrhi::CommandQueue::Graphics);
			}

			device->waitForIdle();

			auto formID = form->GetFormID();

			AddInstance(formID, object, modelName);

			logger::debug("SceneGraph::CreateModelInternal - Commited {} TriShapes to [0x{:08X}]", shapeCount, reinterpret_cast<uintptr_t>(modelPtr));

			return true;
		}
		else {
			logger::warn("SceneGraph::CreateModelInternal - Emplace failed for {} TriShapes", shapeCount);
		}
	}
	else {
		logger::debug("SceneGraph::CreateModelInternal - No TriShapes to commit");
	}

	return false;
}

void SceneGraph::AddInstance(RE::FormID formID, RE::NiAVObject* node, eastl::string path)
{
	logger::debug("SceneGraph::AddInstance [0x{:08X}] - {}, Path: {}", formID, node->name, path);

	auto instanceNodeIt = m_InstanceNodes.find(node);
	if (instanceNodeIt != m_InstanceNodes.end()) {
		logger::warn("SceneGraph::AddInstance - Node already exists: {}", path);
		return;
	}

	auto modelIt = m_Models.find(path);
	if (modelIt == m_Models.end()) {
		logger::warn("SceneGraph::AddInstance - Model doesn't exists: {}", path);
		return;
	}

	auto [instanceIt, emplaced] = m_InstanceNodes.try_emplace(node, nullptr);
	if (!emplaced) {
		logger::warn("SceneGraph::AddInstance - Emplace failed: {}", path);
		return;
	}
	auto instance = eastl::make_unique<Instance>(formID, node, modelIt->second.get());

	if (auto nodesIt = m_InstancesFormIDs.find(formID); nodesIt != m_InstancesFormIDs.end()) {
		nodesIt->second.push_back(instance.get());
	}
	else {
		m_InstancesFormIDs.try_emplace(formID, eastl::vector<Instance*>{ instance.get() });
	}

	instanceIt->second = instance.get();

	m_Instances.emplace_back(eastl::move(instance));

	modelIt->second->AddRef();
}

void SceneGraph::RunGarbageCollection(uint64_t frameIndex)
{
	std::shared_lock lock(m_ReleaseDataMutex);

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

void SceneGraph::InitMSNPipeline()
{
	if (m_MSNPipelineInitialized)
		return;

	auto device = Renderer::GetSingleton()->GetDevice();

	// Compile shaders
	winrt::com_ptr<IDxcBlob> vertexBlob, pixelBlob;
	ShaderUtils::CompileShader(vertexBlob, L"data/shaders/ModelSpaceToTangent.hlsl", {}, L"vs_6_5", L"MainVS");
	ShaderUtils::CompileShader(pixelBlob, L"data/shaders/ModelSpaceToTangent.hlsl", {}, L"ps_6_5", L"MainPS");

	m_MSNVertexShader = device->createShader({ nvrhi::ShaderType::Vertex, "", "MainVS" }, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize());
	m_MSNPixelShader = device->createShader({ nvrhi::ShaderType::Pixel, "", "MainPS" }, pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize());

	// Create sampler
	m_MSNSampler = device->createSampler(
		nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

	// Create binding layout for MSN pass
	auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
		.setVisibility(nvrhi::ShaderType::All)
		.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(uint32_t)))
		.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
		.addItem(nvrhi::BindingLayoutItem::Sampler(0));

	m_MSNBindingLayout = device->createBindingLayout(bindingLayoutDesc);

	m_MSNPipelineInitialized = true;
}

void SceneGraph::ConvertMSN(Model* model, nvrhi::ICommandList* commandList)
{
	InitMSNPipeline();

	auto device = Renderer::GetSingleton()->GetDevice();

	// Group meshes by their converted normal map (descriptor index)
	eastl::unordered_map<DescriptorIndex, eastl::vector<Mesh*>> msnGroups;

	for (auto& mesh : model->meshes) {
		if (mesh->material.shaderFlags.none(RE::BSShaderProperty::EShaderPropertyFlag::kModelSpaceNormals))
			continue;

		auto descHandle = mesh->material.Textures[Constants::Material::NORMALMAP_TEXTURE].texture.lock();
		if (!descHandle)
			continue;

		auto key = descHandle->Get();
		msnGroups[key].push_back(mesh.get());
	}

	for (auto& [allocationIdx, meshes] : msnGroups) {
		auto msnIt = m_MSNAllocationMap.find(allocationIdx);
		if (msnIt == m_MSNAllocationMap.end())
			continue;

		auto normalMapIt = m_NormalMaps.find(msnIt->second);
		if (normalMapIt == m_NormalMaps.end())
			continue;

		auto* normalMap = normalMapIt->second.get();

		if (normalMap->converted)
			continue;

		// Create framebuffer for this render target
		auto framebuffer = device->createFramebuffer(
			nvrhi::FramebufferDesc().addColorAttachment(normalMap->convertedTexture));

		const auto& fbinfo = framebuffer->getFramebufferInfo();

		// Create pipeline lazily (all converted textures share R10G10B10A2_UNORM)
		if (!m_MSNGraphicsPipeline) {
			nvrhi::GraphicsPipelineDesc pipelineDesc;
			pipelineDesc.VS = m_MSNVertexShader;
			pipelineDesc.PS = m_MSNPixelShader;
			pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
			pipelineDesc.bindingLayouts = {
				m_MSNBindingLayout,
				m_TriangleDescriptors->m_Layout,
				m_VertexDescriptors->m_Layout
			};
			pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
			pipelineDesc.renderState.rasterState.setCullNone();

			m_MSNGraphicsPipeline = device->createGraphicsPipeline(pipelineDesc, fbinfo);
		}

		// Create binding set with the source MSN texture
		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(uint32_t)),
			nvrhi::BindingSetItem::Texture_SRV(0, normalMap->sourceTexture),
			nvrhi::BindingSetItem::Sampler(0, m_MSNSampler)
		};
		auto bindingSet = device->createBindingSet(bindingSetDesc, m_MSNBindingLayout);

		// Clear RT to flat normal (0.5, 0.5, 1.0, 1.0)
		commandList->clearTextureFloat(normalMap->convertedTexture, nvrhi::AllSubresources, nvrhi::Color(0.5f, 0.5f, 1.0f, 1.0f));

		for (auto* mesh : meshes) {
			uint32_t geometryIdx = mesh->m_DescriptorHandle.Get();

			nvrhi::GraphicsState state;
			state.pipeline = m_MSNGraphicsPipeline;
			state.framebuffer = framebuffer;
			state.bindings = {
				bindingSet,
				m_TriangleDescriptors->m_DescriptorTable->GetDescriptorTable(),
				m_VertexDescriptors->m_DescriptorTable
			};
			state.viewport.addViewportAndScissorRect(fbinfo.getViewport());

			commandList->setGraphicsState(state);
			commandList->setPushConstants(&geometryIdx, sizeof(geometryIdx));

			nvrhi::DrawArguments args;
			args.vertexCount = mesh->triangleCount * 3;
			args.instanceCount = 1;
			commandList->draw(args);
		}

		normalMap->converted = true;
		m_MSNAllocationMap.erase(allocationIdx);
	}
}
