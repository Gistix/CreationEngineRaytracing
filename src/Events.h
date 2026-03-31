#pragma once

#if defined(SKYRIM)
#include "Types/RE/CellAttachDetachEvent.h"
#endif

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

	class TESCellAttachDetachEventHandler : public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>*);

		static bool Register()
		{
			static TESCellAttachDetachEventHandler singleton;

			auto scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
			scriptEventSourceHolder->GetEventSource<RE::TESCellAttachDetachEvent>()->AddEventSink(&singleton);

			logger::info("Events::Registered {}", typeid(singleton).name());

			return true;
		}
	};
	
	class TESMoveAttachDetachEventHandler : public RE::BSTEventSink<RE::TESMoveAttachDetachEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESMoveAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESMoveAttachDetachEvent>*);

		static bool Register()
		{
			static TESMoveAttachDetachEventHandler singleton;

			auto scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
			scriptEventSourceHolder->GetEventSource<RE::TESMoveAttachDetachEvent>()->AddEventSink(&singleton);

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
