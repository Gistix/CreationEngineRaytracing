#pragma once

#include "RE/B/BSTArray.h"
#include "RE/N/NiSmartPointer.h"
#include "TESWaterReflections.h"

namespace RE
{
	class TESWaterSystem
	{
	public:
		[[nodiscard]] static TESWaterSystem* GetSingleton()
		{
			static REL::Relocation<TESWaterSystem**> singleton{ REL::ID(4796380) };
			return *singleton;
		}

		bool Enable()
		{
			using func_t = bool (*)(TESWaterSystem*);
			static REL::Relocation<func_t> func{ REL::ID(2213949) };
			return func(this);
		}

		// members
		BSTArray<void*>                          unk00;             // 00
		BSTArray<void*>                          unk18;             // 18
		BSTArray<NiPointer<TESWaterReflections>> waterReflections;  // 30
		BSTArray<void*>                          unk48;             // 48
		BSTArray<void*>                          unk60;             // 60
		BSTArray<void*>                          unk78;             // 78
		BSTArray<void*>                          unk90;             // 90
		bool                                     enabled;           // A8
	};
}
