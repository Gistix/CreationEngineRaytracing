#pragma once

#if defined(FALLOUT4)
namespace REL
{
	class RelocationID
	{
	public:
		constexpr RelocationID() noexcept = default;

		explicit constexpr RelocationID(
			[[maybe_unused]] std::uint64_t a_preNGID,
			[[maybe_unused]] std::uint64_t a_postNGID) noexcept
		{
#ifdef FALLOUT_PRE_NG
			_preNGID = a_preNGID;
#endif
#ifdef FALLOUT_POST_NG
			_postNGID = a_postNGID;
#endif
#ifdef FALLOUT_POST_AE
			_postAEID = a_postNGID;
#endif
		}

		explicit constexpr RelocationID(
			[[maybe_unused]] std::uint64_t a_preNGID,
			[[maybe_unused]] std::uint64_t a_postNGID,
			[[maybe_unused]] std::uint64_t a_postAEID) noexcept
		{
#ifdef FALLOUT_PRE_NG
			_preNGID = a_preNGID;
#endif
#ifdef FALLOUT_POST_NG
			_postNGID = a_postNGID;
#endif
#ifdef FALLOUT_POST_AE
			_postAEID = a_postAEID;
#endif
		}

		[[nodiscard]] std::uintptr_t address() const
		{
			auto thisOffset = offset();
			return thisOffset ? base() + offset() : 0;
		}

		[[nodiscard]] std::size_t offset() const
		{
			auto thisID = id();
			return thisID ? IDDB::get().id2offset(thisID) : 0;
		}

		[[nodiscard]] std::uint64_t id() const noexcept
		{
#ifdef FALLOUT_POST_AE
			return _postAEID;
#elif FALLOUT_POST_NG
			return _postNGID;
#elif FALLOUT_PRE_NG
			return _preNGID;
#endif
		}

		[[nodiscard]] explicit operator ID() const noexcept
		{
			return ID(id());
		}

	private:
		[[nodiscard]] static std::uintptr_t base() { return Module::get().base(); }

#ifdef FALLOUT_POST_AE
		std::uint64_t _postAEID{ 0 };
#endif
#if FALLOUT_POST_NG
		std::uint64_t _postNGID{ 0 };
#endif
#if FALLOUT_PRE_NG
		std::uint64_t _preNGID{ 0 };
#endif
	};
}
#endif