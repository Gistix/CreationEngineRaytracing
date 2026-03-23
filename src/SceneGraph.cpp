#include "SceneGraph.h"

#include "Scene.h"

#include "core/Mesh.h"

#include "Renderer.h"
#include "Util.h"

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

			lightData.Type = bsLight->pointLight ? LightType::Point : LightType::Directional;

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

		// Update if applicabe and queue skinning/dynamic update
		instance->model->Update();

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

void SceneGraph::CreateActorModel(RE::Actor* actor, const char* name, RE::NiAVObject* root)
{
	Util::Traversal::ScenegraphFadeNodes(root, [&](RE::BSFadeNode* fadeNode) -> RE::BSVisit::BSVisitControl {
		const bool isRoot = (fadeNode == root);

		auto fadeNodeName = std::format("{}.{}", name, fadeNode->name.c_str());
		CreateModelInternal(actor, isRoot ? name : fadeNodeName.c_str(), fadeNode);

		return RE::BSVisit::BSVisitControl::kContinue;
	});
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
void SceneGraph::ReleaseTexture(ID3D11Texture2D* texture)
{
	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

	m_Textures.erase(texture);
}

void SceneGraph::RemoveInstance(RE::NiAVObject* node)
{
	auto instanceNodeIt = m_InstanceNodes.find(node);

	if (instanceNodeIt == m_InstanceNodes.end())
		return;

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);
	std::unique_lock releaseLock(m_ReleaseDataMutex);

	auto& instance = instanceNodeIt->second;

	instance->model->Release();

	m_InstanceNodes.erase(instanceNodeIt);

	m_InstancesFormIDs.erase(instance->formID);

	// Removes the original instance, all pointers past this point are invalid
	auto instIt = eastl::find_if(
		m_Instances.begin(),
		m_Instances.end(),
		[&](auto& x) { return x.get() == instance; });

	if (instIt != m_Instances.end())
		m_Instances.erase(instIt);
}

void SceneGraph::RemoveInstance(RE::TESForm* form, bool releaseModel)
{
	auto instanceFormIDsIt = m_InstancesFormIDs.find(form->GetFormID());

	// No instance to remove
	if (instanceFormIDsIt == m_InstancesFormIDs.end())
		return;

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

	auto renderer = Renderer::GetSingleton();

	// A single form can hold multiple model instances
	for (auto& instance : instanceFormIDsIt->second) {
		m_InstanceNodes.erase(instance->node);

		auto refCount = instance->model->Release();

		if (refCount <= 0 && releaseModel) {
			logger::debug("SceneGraph::RemoveInstance - {}", instance->model->m_Name);
			//m_Models.erase(instance->model->m_Name);

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

	logger::debug("SceneGraph::SetInstanceDetached - Detaching {}", detached);

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

eastl::shared_ptr<DescriptorHandle> SceneGraph::GetMSNormalMapDescriptor([[maybe_unused]] Mesh* mesh, [[maybe_unused]] RE::BSGraphics::Texture* texture)
{
	return nullptr;
}

void SceneGraph::CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* pRoot)
{
	if (!pRoot)
		return;

	if (!path || strlen(path) == 0)
		return;

	if (m_InstanceNodes.find(pRoot) != m_InstanceNodes.end()) {
		logger::warn("[RT] CreateModel \"{}\" - Instance/Model for 0x{:08X} already present.", path, reinterpret_cast<uintptr_t>(pRoot));
		return;
	}
	
	auto formID = form->GetFormID();

	std::unique_lock lock(Scene::GetSingleton()->m_SceneMutex);

	// We only need one buffer per model
	if (m_Models.find(path) != m_Models.end()) {
		AddInstance(formID, pRoot, path);
		return;
	}

	logger::trace("[RT] CreateModel \"{}\"", typeid(*pRoot).name());

	const auto* bsxFlags = pRoot->GetExtraData<RE::BSXFlags>("BSX");

	if (bsxFlags) {
		if (static_cast<int32_t>(bsxFlags->value) & static_cast<int32_t>(RE::BSXFlags::Flag::kEditorMarker))
			return;

		logger::debug("[RT] CreateModel - BSX Flags [0x{:x}]: {}", bsxFlags->value, Util::GetFlagsString<RE::BSXFlags::Flag>(bsxFlags->value));
	}

	logger::debug("[RT] CreateModel - Path: {}, FormID [0x{:08X}], NiNode [0x{:08X}]: {}", path, formID, reinterpret_cast<uintptr_t>(pRoot), pRoot->name);

	auto formType = form->GetFormType();
	auto baseFormType = formType;

	if (auto* refr = form->AsReference()) {
		if (auto* baseObject = refr->GetBaseObject())
			baseFormType = baseObject->GetFormType();
	}

	auto rootWorldInverse = pRoot->world.Invert();

	eastl::vector<eastl::unique_ptr<Mesh>> meshes;

	// Will traverse and skip non-root fade nodes (and their children)
	auto* validFadeNode = (formType == RE::FormType::ActorCharacter ? reinterpret_cast<RE::BSFadeNode*>(pRoot) : nullptr);

	Util::Traversal::ScenegraphRTGeometries(pRoot, validFadeNode, [&](RE::BSGeometry* pGeometry)->RE::BSVisit::BSVisitControl {
		const char* name = pGeometry->name.c_str();

		logger::trace("\t\t[RT] CreateModel::TraverseScenegraphGeometries - {}", name);

		const auto& geometryType = pGeometry->GetType();

		if (geometryType.none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kDynamicTriShape)) {
			logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Unsupported Geometry: {} for {}", magic_enum::enum_name(geometryType.get()), name);
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		const auto& geometryRuntimeData = pGeometry->GetGeometryRuntimeData();

		auto* effect = geometryRuntimeData.properties[RE::BSGeometry::States::kEffect].get();

		if (!effect) {
			logger::debug("\t\t[RT] CreateModel::TraverseScenegraphGeometries - No Effect");
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		bool isLightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect) != nullptr;
		bool isEffectShader = netimmerse_cast<RE::BSEffectShaderProperty*>(effect) != nullptr;
		bool isWaterShader = netimmerse_cast<RE::BSWaterShaderProperty*>(effect) != nullptr;

		// Only lighting and effect shader for now
		if (!isLightingShader && !isEffectShader && !isWaterShader) {
			logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Unsupported shader type: {}", effect->GetRTTI()->name);
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		auto shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(effect);
		bool skinned = shaderProperty && shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSkinned);

		auto& geomFlags = pGeometry->GetFlags();

		if (geomFlags.any(RE::NiAVObject::Flag::kHidden) && !skinned) {
			logger::debug("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Is Hidden");
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


		float3x4 localToRoot{};

		// Some plants have parts with geometry world position of [0, 0, 0]
		if (pGeometry->world.translate != RE::NiPoint3::Zero())
			XMStoreFloat3x4(&localToRoot, Util::Math::GetXMFromNiTransform(rootWorldInverse * pGeometry->world));

		if (auto* triShapeRD = geometryRuntimeData.rendererData) {  // Non-Skinned
			auto* pTriShape = netimmerse_cast<RE::BSTriShape*>(pGeometry);

			const auto& triShapeRuntime = pTriShape->GetTrishapeRuntimeData();

			if (triShapeRuntime.vertexCount == 0) {
				logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Vertex count of 0 for {}: {}", path ? path : "N/A", name ? name : "N/A");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			if (triShapeRuntime.triangleCount == 0) {
				logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Triangle count of 0 for {}: {}", path ? path : "N/A", name ? name : "N/A");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			auto mesh = eastl::make_unique<Mesh>(flags, name, pGeometry, localToRoot, true, 0);

			mesh->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
			mesh->BuildMaterial(geometryRuntimeData, form);

			meshes.push_back(eastl::move(mesh));
		}
		else if (auto* skinInstance = geometryRuntimeData.skinInstance.get()) {  // Skinned
			// Skinned trees have wrong bone matrices and are causing a device removal due to out of bounds vertices
			//if (baseFormType == RE::FormType::Tree)
			//	return RE::BSVisit::BSVisitControl::kContinue;

			auto& skinPartition = skinInstance->skinPartition;

			if (!skinPartition) {
				logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Invalid SkinPartition");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			if (skinPartition->vertexCount == 0) {
				logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Vertex count of 0 for {}: {}", path ? path : "N/A", name ? name : "N/A");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			const auto skinNumPartitions = skinPartition->numPartitions;

			logger::debug("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Partitions: {}, VertexCount: {}, Unk24: [0x{:X}]", skinNumPartitions, skinPartition->vertexCount, skinPartition->unk24);

			// This looks diabolical
			static REL::Relocation<const RE::NiRTTI*> dismemberRTTI{ RE::BSDismemberSkinInstance::Ni_RTTI };

			eastl::vector<RE::BSDismemberSkinInstance::Data> dismemberData(skinNumPartitions, { true, false, 0 });

			decltype(dismemberReferences.begin()) it;
			bool emplacedDismemberRef = false;

			if (skinInstance->GetRTTI() == dismemberRTTI.get()) {
				auto* dismemberSkinInstance = reinterpret_cast<RE::BSDismemberSkinInstance*>(skinInstance);

				auto& dismemberRuntime = dismemberSkinInstance->GetRuntimeData();

				const auto dismemberNumPartitions = static_cast<uint32_t>(dismemberRuntime.numPartitions);

				if (skinNumPartitions != dismemberNumPartitions)
					logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Skin and Dismember partition count mismatch");

				std::memcpy(dismemberData.data(), dismemberRuntime.partitions, dismemberNumPartitions * sizeof(RE::BSDismemberSkinInstance::Data));

				eastl::tie(it, emplacedDismemberRef) = dismemberReferences.try_emplace(dismemberSkinInstance, eastl::vector<Mesh*>(skinNumPartitions));
			}

			for (size_t i = 0; i < skinPartition->partitions.size(); i++) {
				auto& partition = skinPartition->partitions[i];
				auto& dismemberPartition = dismemberData[i];

				// Fix for modded geometry
				if (partition.triangles == 0) {
					logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Triangle count of 0 for {}: {}", path ? path : "N/A", name ? name : "N/A");
					continue;
				}

				// Fix for modded geometry
				if (partition.bonesPerVertex > 0)
					flags |= Mesh::Flags::Skinned;

				auto mesh = eastl::make_unique<Mesh>(flags, name, pGeometry, localToRoot, dismemberPartition.editorVisible, dismemberPartition.slot);

				// Diabolical Part II
				if (emplacedDismemberRef)
					it->second[i] = mesh.get();

				mesh->BuildMesh(partition.buffData, skinPartition->vertexCount, partition.triangles, partition.bonesPerVertex);
				mesh->BuildMaterial(geometryRuntimeData, form);

				meshes.push_back(eastl::move(mesh));
			}
		}

		return RE::BSVisit::BSVisitControl::kContinue;
		});

	if (auto shapeCount = meshes.size(); shapeCount > 0) {
		auto model = eastl::make_unique<Model>(path, pRoot, form, meshes);

		auto& modelName = model->m_Name;

		auto [it, emplaced] = m_Models.try_emplace(modelName, eastl::move(model));

		if (emplaced) {
			auto* modelPtr = it->second.get();

			if (modelPtr->ShouldQueueMSNConversion())
				m_MSNConvertionQueue.emplace_back(modelName);

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

			//computeCommandList->compactBottomLevelAccelStructs();

			computeCommandList->close();

			device->queueWaitForCommandList(nvrhi::CommandQueue::Compute, nvrhi::CommandQueue::Copy, copySubmittedInstance);

			device->executeCommandList(computeCommandList, nvrhi::CommandQueue::Compute);

			device->waitForIdle();

			AddInstance(formID, pRoot, modelName);

			logger::debug("[RT] CreateModel - Commited {} TriShapes to [0x{:08X}]", shapeCount, reinterpret_cast<uintptr_t>(modelPtr));
		}
		else {
			logger::warn("[RT] CreateModel - Emplace failed for {} TriShapes", shapeCount);
		}
	}
	else {
		logger::debug("[RT] CreateModel - No TriShapes to commit");
	}
}

void SceneGraph::AddInstance(RE::FormID formID, RE::NiAVObject* node, eastl::string path)
{
	logger::debug("SceneGraph::AddInstance [0x{:08X}] - {}, Path: {}", formID, node->name, path);

	auto instanceNodeIt = m_InstanceNodes.find(node);
	if (instanceNodeIt != m_InstanceNodes.end()) {
		logger::warn("SceneGraph::AddInstance - Instance already exists: {}", path);
		return;
	}

	auto modelIt = m_Models.find(path);
	if (modelIt == m_Models.end()) {
		logger::warn("SceneGraph::AddInstance - Model already exists: {}", path);
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
