#pragma once

#include "Constants.h"

#include "Utils/Game.h"
#include "Utils/Geometry.h"
#include "Utils/Material.h"
#include "Utils/Traversal.h"
#include "Utils/Math.h"

#include "magic_enum_spec.h"

namespace Util
{
	bool IsPlayerFormID(RE::FormID formID);

	bool IsPlayer(RE::TESForm* form);

	std::string WStringToString(const std::wstring& wideString);

	std::wstring StringToWString(const std::string& str);

	template <typename T>
	std::string GetFlagsString(auto value)
	{
		static_assert(
			magic_enum::customize::enum_range<T>::is_flags,
			"T must be a magic_enum flags enum");

		using N = decltype(value);

		const auto& entries = magic_enum::enum_entries<T>();

		std::string flags = "";

		for (const auto& [flag, name] : entries) {
			if (value & static_cast<N>(flag)) {
				flags += fmt::format("{} ", name);
			}
		}

		return flags;
	};

	template <typename T>
	auto CreateStructuredBuffer(nvrhi::IDevice* device, uint32_t maxCapacity, const char* name, bool uav = false) {
		auto size = static_cast<uint32_t>(sizeof(T));

		auto bufferDesc = nvrhi::BufferDesc()
			.setByteSize(size * maxCapacity)
			.setStructStride(size)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
			.setDebugName(name);

		if (uav)
			bufferDesc.setCanHaveUAVs(true);

		return device->createBuffer(bufferDesc);
	};
}