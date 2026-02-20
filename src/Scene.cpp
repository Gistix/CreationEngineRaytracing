#include "Scene.h"
#include "Util.h"

#include "SceneGraph.h"

#include "framework/DescriptorTableManager.h"

SceneGraph* Scene::GetSceneGraph() const
{
	return m_SceneGraph.get();
}

void Scene::Update([[maybe_unused]] nvrhi::ICommandList* commandList)
{
	auto sceneGraph = GetSceneGraph();

	for (auto& instance : sceneGraph->GetInstances())
	{
		instance->Update();
	}

	for (auto& [path, model] : sceneGraph->GetModels())
	{
		model->Update();
	}
}

void Scene::AttachModel([[maybe_unused]] RE::TESForm* form) 
{
	auto* refr = form->AsReference();

	auto* baseObject = refr->GetBaseObject();

	//auto flags = baseObject->GetFormFlags();
	RE::FormType type = baseObject->GetFormType();

	if (type == RE::FormType::IdleMarker)
		return;

	if (baseObject->IsMarker())
		return;

	auto* node = refr->Get3D();

	if (!node)
		return;

	if (auto* model = baseObject->As<RE::TESModel>()) {
		logger::info("[Raytracing] AttachModel - Model: {}", model->model);
		GetSceneGraph()->CreateModel(refr, model->GetModel(), node);
		return;
	}

	if (Util::IsPlayer(refr)) {
		if (auto* player = reinterpret_cast<RE::PlayerCharacter*>(refr)) {
			// First Person
			//rt.CreateModelInternal(refr, std::format("{}_1stPerson", name).c_str(), pNiAVObject);

			// Third Person
			//rt.CreateActorModel(player, name, player->Get3D(false));
			logger::info("[Raytracing] AttachModel - Player: {}", player->GetName());
			return;
		}
	}

	if (auto* actor = refr->As<RE::Actor>()) {
		//rt.CreateActorModel(actor, actor->GetName(), pNiAVObject);
		logger::info("[Raytracing] AttachModel - Actor: {}", actor->GetName());
	}
}

void Scene::AttachLand([[maybe_unused]] RE::TESForm* form, [[maybe_unused]] RE::NiAVObject* root) 
{

}