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
#	if defined(FALLOUT_POST_NG)
#		include "REX/REX/Singleton.h"
#	endif
namespace logger = F4SE::log;
#endif

#pragma warning(pop)

#define WIN32_LEAN_AND_MEAN
#include "Windows.h"

#define NOMINMAX

#include <shared_mutex>

#include <xbyak/xbyak.h>

#include <winrt/base.h>

#include <string>
using namespace std::literals;

namespace stl
{
	using namespace SKSE::stl;
};

#include <directx/d3d12.h>

#include <nvrhi/d3d12.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/utils.h>

#include <magic_enum/magic_enum.hpp>

#include <new>
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line);
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line);

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/bitset.h>
#include <EASTL/bonus/fixed_ring_buffer.h>
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

#if defined(CERT_EXPORTS)
#define CERT_API __declspec(dllexport)
#else
#define CERT_API __declspec(dllimport)
#endif