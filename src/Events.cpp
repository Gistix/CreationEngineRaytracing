#include "Events.h"
#include "Scene.h"
#include "Renderer.h"
#include "Util.h"

namespace Events
{
#if defined(SKYRIM)
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

	RE::BSEventNotifyControl Events::TESCellAttachDetachEventHandler::ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>*)
	{
		if (!a_event)
			return RE::BSEventNotifyControl::kContinue;

		auto* eventRef = a_event->reference.get();
		
		logger::trace("TESCellAttachDetachEventHandler::ProcessEvent - 0x{:08X}, Attached {} to 0x{:08X}", eventRef->GetFormID(), a_event->attached, eventRef->parentCell ? eventRef->parentCell->GetFormID() : 0);

		Scene::GetSingleton()->GetSceneGraph()->SetInstanceDetached(eventRef, !a_event->attached);

		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl Events::TESMoveAttachDetachEventHandler::ProcessEvent(const RE::TESMoveAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESMoveAttachDetachEvent>*)
	{
		if (!a_event)
			return RE::BSEventNotifyControl::kContinue;

		auto* eventRef = a_event->movedRef.get();

		logger::trace("TESMoveAttachDetachEventHandler::ProcessEvent - 0x{:08X}, Cell Attached {} to 0x{:08X}", eventRef->GetFormID(), a_event->isCellAttached, eventRef->parentCell ? eventRef->parentCell->GetFormID() : 0);

		if (a_event->isCellAttached)
			Scene::GetSingleton()->GetSceneGraph()->SetInstanceDetached(eventRef, false);

		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl Events::CellAttachDetachEventHandler::ProcessEvent(const RE::CellAttachDetachEvent* a_event, RE::BSTEventSource<RE::CellAttachDetachEvent>*)
	{
		bool attached = a_event->status == RE::CellAttachDetachEvent::Status::FinishAttach;
		bool detaching = a_event->status == RE::CellAttachDetachEvent::Status::StartDetach;

		logger::trace("CellAttachDetachEventHandler::ProcessEvent - 0x{:08X}, Status {}", a_event->cell->GetFormID(), magic_enum::enum_name(a_event->status));

		if (!attached && !detaching)
			return RE::BSEventNotifyControl::kContinue;

		auto& runtimeData = a_event->cell->GetRuntimeData();

		auto* land = runtimeData.cellLand;

		if (!land)
			return RE::BSEventNotifyControl::kContinue;

		Scene::GetSingleton()->GetSceneGraph()->SetInstanceDetached(land, detaching);

		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl Events::TESEquipEventHandler::ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*)
	{
		if (!a_event)
			return RE::BSEventNotifyControl::kContinue;


		logger::info("TESEquipEventHandler::ProcessEvent - {} {}", magic_enum::enum_name(a_event->actor->GetFormType()), a_event->actor->GetName());

		auto* baseObject = RE::TESForm::LookupByID(a_event->baseObject);

		logger::info("TESEquipEventHandler::ProcessEvent - Type: {}, Name: {}, Equipped {}", magic_enum::enum_name(baseObject->GetFormType()), baseObject->GetName(), a_event->equipped);

		//auto* equippedItem = RE::TESForm::LookupByID<RE::TESBoundObject>(a_event->baseObject);

		return RE::BSEventNotifyControl::kContinue;
	}
#endif

	void Register()
	{
#if defined(SKYRIM)
		TESObjectLoadedEventHandler::Register();

		// Reference event, also fired when the cell event fires
		TESCellAttachDetachEventHandler::Register();

		// Reference moved between cells
		TESMoveAttachDetachEventHandler::Register();

		// Cell event (inside/outside transitions detaches entire cells)
		CellAttachDetachEventHandler::Register();
#elif defined(FALLOUT4)
#	if defined(FALLOUT_POST_NG)

#	endif
#endif
		logger::info("All events registered");
	}
}
