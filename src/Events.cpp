#include "Events.h"
#include "Scene.h"
#include "Renderer.h"
#include "Util.h"

namespace Events
{
	RE::BSEventNotifyControl Events::TESObjectLoadedEventHandler::ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*)
	{
		if (!a_event)
			return RE::BSEventNotifyControl::kContinue;

		auto* eventRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->formID);

		if (a_event->loaded)
			return RE::BSEventNotifyControl::kContinue;

		Scene::GetSingleton()->GetSceneGraph()->RemoveInstance(eventRef, true);

		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl Events::CellAttachDetachEventHandler::ProcessEvent(const RE::CellAttachDetachEvent* a_event, RE::BSTEventSource<RE::CellAttachDetachEvent>*)
	{
		bool attaching = a_event->status == RE::CellAttachDetachEvent::Status::StartAttach;
		bool detaching = a_event->status == RE::CellAttachDetachEvent::Status::StartDetach;

		if (!attaching && !detaching)
			return RE::BSEventNotifyControl::kContinue;

		auto& runtimeData = a_event->cell->GetRuntimeData();

		for (auto& reference : runtimeData.references) {
			Scene::GetSingleton()->GetSceneGraph()->SetInstanceDetached(reference.get(), detaching);
		}

		auto* land = runtimeData.cellLand;

		if (!land)
			return RE::BSEventNotifyControl::kContinue;

		Scene::GetSingleton()->GetSceneGraph()->SetInstanceDetached(land, detaching);

		return RE::BSEventNotifyControl::kContinue;
	}

	void Register()
	{
#if defined(SKYRIM)
		CellAttachDetachEventHandler::Register();
		TESObjectLoadedEventHandler::Register();
#elif defined(FALLOUT4)
#	if defined(FALLOUT_POST_NG)

#	endif
#endif
		logger::info("All events registered");
	}
}
