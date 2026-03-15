#pragma once

#include "Types/RE/CellAttachDetachEvent.h"

namespace Events
{
#if defined(SKYRIM)
	class TESObjectLoadedEventHandler : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*);

		static bool Register()
		{
			static TESObjectLoadedEventHandler singleton;

			auto scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
			scriptEventSourceHolder->GetEventSource<RE::TESObjectLoadedEvent>()->AddEventSink(&singleton);

			logger::info("Events::Registered {}", typeid(singleton).name());

			return true;
		}
	};

	class CellAttachDetachEventHandler : public RE::BSTEventSink<RE::CellAttachDetachEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::CellAttachDetachEvent* a_event, RE::BSTEventSource<RE::CellAttachDetachEvent>*);

		static bool Register()
		{
			static CellAttachDetachEventHandler singleton;

			auto* tes = RE::TES::GetSingleton();
			tes->AddEventSink<RE::CellAttachDetachEvent>(&singleton);

			logger::info("Events::Registered {}", typeid(singleton).name());

			return true;
		}
	};

#elif defined(FALLOUT4)

#endif

	void Register();
}
