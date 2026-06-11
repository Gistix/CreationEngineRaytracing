#pragma once

#undef DEBUG

#pragma warning(push)

#if defined(SKYRIM)
#	include <SKSE/SKSE.h>
#	include <RE/Skyrim.h>
namespace logger = SKSE::log;
#elif defined(FALLOUT4)
#	include "F4SE/F4SE.h"
#	include "RE/Fallout.h"

#   include "REX/W32/OLE32.h"
#   include "REX/W32/SHELL32.h"
#   include <spdlog/spdlog.h>;
namespace logger
{
    template <class... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args)
    {
        REX::TRACE<Args...>{fmt, std::forward<Args>(args)...};
    }

    inline void trace(std::string_view msg)
    {
        REX::TRACE<void>{msg};
    }

    template <class... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args)
    {
        REX::DEBUG<Args...>{fmt, std::forward<Args>(args)...};
    }

    inline void debug(std::string_view msg)
    {
        REX::DEBUG<void>{msg};
    }

    template <class... Args>
    void info(std::format_string<Args...> fmt, Args&&... args)
    {
        REX::INFO<Args...>{fmt, std::forward<Args>(args)...};
    }

    inline void info(std::string_view msg)
    {
        REX::INFO<void>{msg};
    }

    template <class... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args)
    {
        REX::WARN<Args...>{fmt, std::forward<Args>(args)...};
    }

    inline void warn(std::string_view msg)
    {
        REX::WARN<void>{msg};
    }

    template <class... Args>
    void error(std::format_string<Args...> fmt, Args&&... args)
    {
        REX::ERROR<Args...>{fmt, std::forward<Args>(args)...};
    }

    inline void error(std::string_view msg)
    {
        REX::ERROR<void>{msg};
    }

    template <class... Args>
    void critical(std::format_string<Args...> fmt, Args&&... args)
    {
        REX::CRITICAL<Args...>{fmt, std::forward<Args>(args)...};
    }

    inline void critical(std::string_view msg)
    {
        REX::CRITICAL<void>{msg};
    }

    static std::optional<std::filesystem::path> log_directory()
    {
        wchar_t* buffer{ nullptr };
        const auto result = REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(buffer));
        std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(buffer, REX::W32::CoTaskMemFree);
        if (!knownPath || result != 0) {
            error("Failed to get known folder path");
            return std::nullopt;
        }

        std::filesystem::path path = knownPath.get();
        path /= std::format("My Games/{}/F4SE", F4SE::GetSaveFolderName());
        return path;
    }
}
#endif

#include "adapter.h"

#pragma warning(pop)

#define WIN32_LEAN_AND_MEAN
#include "Windows.h"

#define NOMINMAX

#include <shared_mutex>

#include <xbyak/xbyak.h>

#include <detours/detours.h>

#include <winrt/base.h>

#include <string>
using namespace std::literals;

#include "stl.h"

#include <d3d11_4.h>

#include <directx/d3d12.h>

#include <nvrhi/d3d12.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/utils.h>
#include <nvrhi/validation.h>

#include <magic_enum/magic_enum.hpp>

using namespace magic_enum::bitwise_operators;

#include <new>
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line);
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line);

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/bitset.h>
#include <EASTL/bonus/fixed_ring_buffer.h>
#include <EASTL/deque.h>
#include <EASTL/fixed_list.h>
#include <EASTL/fixed_slist.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/functional.h>
#include <EASTL/hash_set.h>
#include <EASTL/map.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/set.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/tuple.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#ifdef NDEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif

#include "SimpleMath.h"

using float2 = DirectX::SimpleMath::Vector2;
using float3 = DirectX::SimpleMath::Vector3;
using float4 = DirectX::SimpleMath::Vector4;
using float3x4 = DirectX::XMFLOAT3X4;
using float4x4 = DirectX::SimpleMath::Matrix;
using uint = uint32_t;

#include "Types.h"

#if defined(CERT_EXPORTS)
#define CERT_API __declspec(dllexport)
#else
#define CERT_API __declspec(dllimport)
#endif