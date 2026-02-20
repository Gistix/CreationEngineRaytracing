#pragma once

#include "Constants.h"

#include "Utils/Game.h"
#include "Utils/Geometry.h"
#include "Utils/Traversal.h"

#include "magic_enum_spec.h"

namespace Util
{
	bool IsPlayerFormID(RE::FormID formID);

	bool IsPlayer(RE::TESForm* form);

	std::string WStringToString(const std::wstring& wideString);

	std::wstring StringToWString(const std::string& str);

	float3 Float3(RE::NiPoint3 niPoint);

	float3 Normalize(float3 vector);

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

	DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform);

	uint2 GetDispatchCount(uint2 resolution, float threads = 8.0f);
}