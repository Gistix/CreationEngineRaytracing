#pragma once

#if defined(FALLOUT4)

#include "RE/N/NiExtraData.h"
#include "RE/M/MemoryManager.h"
#include <vector>

namespace RE
{
	class NiFloatsExtraData : public NiExtraData
	{
	public:
		inline static constexpr auto RTTI{ RTTI::NiFloatsExtraData };
		inline static constexpr auto VTABLE{ VTABLE::NiFloatsExtraData };
		inline static constexpr auto Ni_RTTI{ Ni_RTTI::NiFloatsExtraData };

		~NiFloatsExtraData() override
		{
			if (data) {
				MemoryManager::GetSingleton().Deallocate(data, false);
				data = nullptr;
			}
		}

		static NiFloatsExtraData* Create(const BSFixedString& a_name, const std::vector<float>& a_floats)
		{
			auto* extraData = new NiFloatsExtraData();
			extraData->name = a_name;
			extraData->size = static_cast<std::uint32_t>(a_floats.size());
			if (extraData->size > 0) {
				extraData->data = static_cast<float*>(MemoryManager::GetSingleton().Allocate(extraData->size * sizeof(float), 0, false));
				std::memcpy(extraData->data, a_floats.data(), extraData->size * sizeof(float));
			} else {
				extraData->data = nullptr;
			}
			return extraData;
		}

		// members
		std::uint32_t size;   // 18
		std::uint32_t pad1C;  // 1C
		float* data;          // 20
	};
	static_assert(sizeof(NiFloatsExtraData) == 0x28);
}

#endif