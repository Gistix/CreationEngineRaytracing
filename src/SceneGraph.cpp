#include "SceneGraph.h"

#include "core/Shape.h"

#include "Util.h"

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
	TraverseScenegraphFadeNodes(root, [&](RE::BSFadeNode* fadeNode) -> RE::BSVisit::BSVisitControl {
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

	logger::trace("[RT] TESObjectLAND_Attach3D - {}", std::format("Landscape_{}_{}", exteriorData->cellX, exteriorData->cellY).c_str());

	for (uint i = 0; i < 4; i++) {
		auto mesh = loadedData->mesh[i];

		if (!mesh)
			continue;

		CreateModelInternal(land, std::format("Landscape_{}_{}_Quad_{}", exteriorData->cellX, exteriorData->cellY, i).c_str(), mesh);
	}
}

void SceneGraph::CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* pRoot)
{
	if (!pRoot)
		return;

	if (!path || strlen(path) == 0)
		return;

	if (nodeInstances.find(pRoot) != nodeInstances.end()) {
		logger::warn("[RT] CreateModel \"{}\" - Instance/Model for 0x{:08X} already present.", path, reinterpret_cast<uintptr_t>(pRoot));
		return;
	}

	auto formID = form->GetFormID();

	// We only need one buffer per model
	if (models.find(path) != models.end()) {
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

	auto rootWorldInverse = pRoot->world.Invert();

	eastl::vector<eastl::unique_ptr<Shape>> shapes;

	// Will traverse and skip non-root fade nodes (and their children)
	auto* validFadeNode = (formType == RE::FormType::ActorCharacter ? reinterpret_cast<RE::BSFadeNode*>(pRoot) : nullptr);

	TraverseScenegraphRTGeometries(pRoot, validFadeNode, [&](RE::BSGeometry* pGeometry)->RE::BSVisit::BSVisitControl {
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

		// Only lighting and effect shader for now
		if (!isLightingShader && !isEffectShader) {
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

		auto flags = Shape::Flags::None;

		// Landscape needs special handling of triangles
		if (formType == RE::FormType::Land)
			flags |= Shape::Flags::Landscape;

		if (geometryType.all(RE::BSGeometry::Type::kDynamicTriShape))
			flags |= Shape::Flags::Dynamic;

		float3x4 localToRoot;
		XMStoreFloat3x4(&localToRoot, Util::GetXMFromNiTransform(rootWorldInverse * pGeometry->world));

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

			auto shape = eastl::make_unique<Shape>(flags, shapeRegisters.Allocate(), pGeometry, localToRoot);

			shape->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
			shape->BuildMaterial(geometryRuntimeData, name, formID);
			shape->CreateBuffers(Util::StringToWString(name));

			shapes.push_back(eastl::move(shape));
		}
		else if (auto* skinInstance = geometryRuntimeData.skinInstance.get()) {  // Skinned
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

				eastl::tie(it, emplacedDismemberRef) = dismemberReferences.try_emplace(dismemberSkinInstance, eastl::vector<Shape*>(skinNumPartitions));
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
					flags |= Shape::Flags::Skinned;

				auto shape = eastl::make_unique<Shape>(flags, shapeRegisters.Allocate(), pGeometry, localToRoot, dismemberPartition.editorVisible, dismemberPartition.slot);

				// Diabolical Part II
				if (emplacedDismemberRef)
					it->second[i] = shape.get();

				shape->BuildMesh(partition.buffData, skinPartition->vertexCount, partition.triangles, partition.bonesPerVertex);
				shape->BuildMaterial(geometryRuntimeData, name, formID);
				shape->CreateBuffers(name);

				shapes.push_back(eastl::move(shape));
			}
		}

		return RE::BSVisit::BSVisitControl::kContinue;
		});

	if (auto shapeCount = shapes.size(); shapeCount > 0) {
		eastl::string modelKey = path;

		auto model = eastl::make_unique<Model>(shapes);

		// Models with these flags cannot be instanced directly
		if (model->GetShapeFlags().any(Shape::Flags::Dynamic, Shape::Flags::Skinned))
			modelKey.append(Model::KeySuffix(pRoot).c_str());

		auto [it, emplaced] = models.try_emplace(modelKey, eastl::move(model));

		if (emplaced) {
			if (it->second->ShouldQueueMSNConversion())
				msnConvertionQueue.emplace_back(modelKey);

			it->second->BuildBLAS(commandList.get());

			AddInstance(formID, pRoot, modelKey);

			logger::debug("[RT] CreateModel - Commited {} TriShapes to [0x{:08X}]", shapeCount, reinterpret_cast<uintptr_t>(it->second.get()));
		}
		else {
			logger::warn("[RT] CreateModel - Emplace failed for {} TriShapes", shapeCount);
		}
	}
	else {
		logger::debug("[RT] CreateModel - No TriShapes to commit");
	}
}

static RE::BSVisit::BSVisitControl TraverseScenegraphFadeNodes(RE::NiAVObject* a_object, std::function<RE::BSVisit::BSVisitControl(RE::BSFadeNode*)> a_func)
{
	auto result = RE::BSVisit::BSVisitControl::kContinue;

	if (!a_object) {
		return result;
	}

	auto fadeNode = a_object->AsFadeNode();
	if (fadeNode) {
		result = a_func(fadeNode);

		if (result == RE::BSVisit::BSVisitControl::kStop) {
			return result;
		}
	}

	auto node = a_object->AsNode();
	if (node) {
		for (auto& child : node->GetChildren()) {
			result = TraverseScenegraphFadeNodes(child.get(), a_func);
			if (result == RE::BSVisit::BSVisitControl::kStop) {
				break;
			}
		}
	}

	return result;
}

// A custom visit controller built to ignore billboard/particle geometry
static RE::BSVisit::BSVisitControl TraverseScenegraphRTGeometries(RE::NiAVObject* a_object, RE::BSFadeNode* validFadeNode, std::function<RE::BSVisit::BSVisitControl(RE::BSGeometry*)> a_func)
{
	auto result = RE::BSVisit::BSVisitControl::kContinue;

	if (!a_object) {
		return result;
	}

	auto geom = a_object->AsGeometry();
	if (geom) {
		return a_func(geom);
	}

	// Doodlum sez this is faster
	auto rtti = a_object->GetRTTI();

	static REL::Relocation<const RE::NiRTTI*> billboardRTTI{ RE::NiBillboardNode::Ni_RTTI };
	if (rtti == billboardRTTI.get())
		return result;

	// Might break vegetation
	static REL::Relocation<const RE::NiRTTI*> orderedRTTI{ RE::BSOrderedNode::Ni_RTTI };
	if (rtti == orderedRTTI.get())
		return result;

	auto node = a_object->AsNode();
	if (node) {
		for (auto& child : node->GetChildren()) {
			if (!child)
				continue;

			if (validFadeNode) {
				if (auto fadeNode = child->AsFadeNode(); fadeNode && fadeNode != validFadeNode) {
					continue;
				}
			}

			result = TraverseScenegraphRTGeometries(child.get(), validFadeNode, a_func);
			if (result == RE::BSVisit::BSVisitControl::kStop) {
				break;
			}
		}
	}

	return result;
}
