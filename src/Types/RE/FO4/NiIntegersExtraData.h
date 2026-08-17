#pragma once

#if defined(FALLOUT4)

#include "RE/N/NiExtraData.h"
#include "RE/M/MemoryManager.h"
#include <vector>

namespace RE
{
	class NiIntegersExtraData : public NiExtraData
	{
	public:
		inline static constexpr auto RTTI{ RTTI::NiIntegersExtraData };
		inline static constexpr auto VTABLE{ VTABLE::NiIntegersExtraData };
		inline static constexpr auto Ni_RTTI{ Ni_RTTI::NiIntegersExtraData };

		~NiIntegersExtraData() override
		{
			if (value) {
				MemoryManager::GetSingleton().Deallocate(value, false);
				value = nullptr;
			}
		}

		static NiIntegersExtraData* Create(const BSFixedString& a_name, const std::vector<std::int32_t>& a_integers)
		{
			auto* data = new NiIntegersExtraData();
			data->name = a_name;
			data->size = static_cast<std::uint32_t>(a_integers.size());
			if (data->size > 0) {
				data->value = static_cast<std::int32_t*>(MemoryManager::GetSingleton().Allocate(data->size * sizeof(std::int32_t), 0, false));
				std::memcpy(data->value, a_integers.data(), data->size * sizeof(std::int32_t));
			} else {
				data->value = nullptr;
			}
			return data;
		}

		// members
		std::uint32_t size;   // 18
		std::uint32_t pad1C;  // 1C
		std::int32_t* value;  // 20
	};
	static_assert(sizeof(NiIntegersExtraData) == 0x28);
}

#endif
