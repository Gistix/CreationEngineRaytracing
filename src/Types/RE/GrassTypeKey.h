#pragma once

#include "PCH.h"

namespace RE
{
	struct GrassTypeKey
	{
		[[nodiscard]] bool operator==(const GrassTypeKey&) const = default;

		// members
		RE::FormID      id;  // 00
		std::int16_t cellX;  // 04
		std::int16_t cellY;  // 06
	};
	static_assert(sizeof(GrassTypeKey) == 0x8);

	template <>
	struct BSCRC32_<GrassTypeKey>
	{
	public:
		[[nodiscard]] std::uint32_t operator()(const GrassTypeKey& a_key) const noexcept
		{
			return detail::GenerateCRC32(
				std::span(
					reinterpret_cast<const std::uint8_t*>(std::addressof(a_key)),
					sizeof(GrassTypeKey)));
		}
	};
}

namespace eastl
{
	template <>
	struct hash<RE::GrassTypeKey>
	{
		size_t operator()(const RE::GrassTypeKey& k) const noexcept
		{
			uint64_t packed;
			std::memcpy(&packed, &k, sizeof(k));
			return eastl::hash<uint64_t>{}(packed);
		}
	};
}