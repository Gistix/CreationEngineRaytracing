#include "Scene.h"
#include "Util.h"
#include "SceneGraph.h"

#include "framework/DescriptorTableManager.h"

#include "Renderer.h"

SceneGraph* Scene::GetSceneGraph() const
{
	return m_SceneGraph.get();
}

bool Scene::Initialize(RendererParams rendererParams) {
	auto* renderer = Renderer::GetSingleton();

	// Initialize renderer
	renderer->Initialize(rendererParams);

	if (!renderer->GetDevice())
		return false;

	// Initialize global descriptors (mesh and texture bindless arrays)
	m_SceneGraph->Initialize();

	renderer->InitDefaultTextures();

	// We split render pass initialization from renderer because of the global descriptors
	renderer->InitRenderPasses();

	return true;
}

void Scene::Update([[maybe_unused]] nvrhi::ICommandList* commandList)
{
	GetSceneGraph()->Update(commandList);
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

void Scene::AddLight(RE::BSLight* light)
{
	GetSceneGraph()->AddLight(light);
}

void Scene::RemoveLight(const RE::NiPointer<RE::BSLight>& light)
{
	GetSceneGraph()->RemoveLight(light.get());
}