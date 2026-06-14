#pragma once

#include <type_traits>
#include <cstdint>

template <typename Enum>
class EnumFlags {
    static_assert(std::is_enum_v<Enum>, "EnumFlags<T> requires an enum type");

public:
    using Underlying = std::underlying_type_t<Enum>;

    constexpr EnumFlags() : bits(0) {}
    constexpr EnumFlags(Enum e) : bits(toBit(e)) {}
    constexpr EnumFlags(Underlying e) : bits(e) {}

    constexpr EnumFlags& operator|=(Enum e) {
        bits |= toBit(e);
        return *this;
    }

    constexpr EnumFlags& operator&=(Enum e) {
        bits &= toBit(e);
        return *this;
    }

    template <class... Args>
    constexpr EnumFlags& set(Args... a_args) noexcept
        requires(std::same_as<Args, Enum> && ...)
    {
        bits |= (toBit(a_args) | ...);
        return *this;
    }

    template <class... Args>
    constexpr EnumFlags& set(bool a_set, Args... a_args) noexcept
        requires(std::same_as<Args, Enum> && ...)
    {
        if (a_set)
            bits |= (toBit(a_args) | ...);
        else
            bits &= ~(toBit(a_args) | ...);

        return *this;
    }

    template <class... Args>
    [[nodiscard]] constexpr bool any(Args... a_args) const noexcept
        requires(std::same_as<Args, Enum> && ...)
    {
        const Underlying mask = (toBit(a_args) | ...);
        return (bits & mask) != 0;
    }

    template <class... Args>
    [[nodiscard]] constexpr bool all(Args... a_args) const noexcept
        requires(std::same_as<Args, Enum> && ...)
    {
        const Underlying mask = (toBit(a_args) | ...);
        return (bits & mask) == mask;
    }

    template <class... Args>
    [[nodiscard]] constexpr bool none(Args... a_args) const noexcept
        requires(std::same_as<Args, Enum> && ...)
    {
        const Underlying mask = (toBit(a_args) | ...);
        return (bits & mask) == 0;
    }

    constexpr void clear(Enum e) {
        bits &= ~toBit(e);
    }

    constexpr void reset() {
        bits = 0;
    }

private:
    Underlying bits;

    static constexpr Underlying toBit(Enum e) {
        return Underlying(1) << static_cast<Underlying>(e);
    }
};

template <typename Enum>
constexpr EnumFlags<Enum> operator|(EnumFlags<Enum> a, Enum b) {
    a |= b;
    return a;
}