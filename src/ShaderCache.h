#pragma once

#include <dxcapi.h>

#include "Types/ShaderDefine.h"
#include "Types/ShaderStage.h"

namespace ShaderCache
{
	struct ShaderKey
	{
		eastl::wstring filePath;
		eastl::wstring target;
		eastl::wstring entryPoint;
		bool isVulkan;
		eastl::vector<ShaderDefine> defines;

		ShaderKey(const wchar_t* fp, const eastl::vector<DxcDefine>& a_defines, const wchar_t* tgt, const wchar_t* ep, bool vk)
			: filePath(fp), target(tgt), entryPoint(ep), isVulkan(vk)
		{
			defines.reserve(a_defines.size());
			for (auto& define : a_defines) {
				defines.emplace_back(define.Name ? define.Name : L"", define.Value ? define.Value : L"");
			}
			eastl::sort(defines.begin(), defines.end(), [](const auto& a, const auto& b) {
				return a.name < b.name;
			});
		}

		bool operator==(const ShaderKey& other) const
		{
			return filePath == other.filePath &&
				target == other.target &&
				entryPoint == other.entryPoint &&
				isVulkan == other.isVulkan &&
				defines == other.defines;
		}
	};

	inline void HashCombine(size_t& h, size_t v)
	{
		h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2);
	}

	struct ShaderKeyHash
	{
		size_t operator()(const ShaderKey& key) const
		{
			size_t h = eastl::hash<eastl::wstring>{}(key.filePath);

			HashCombine(h, eastl::hash<eastl::wstring>{}(key.target));
			HashCombine(h, eastl::hash<eastl::wstring>{}(key.entryPoint));
			HashCombine(h, static_cast<size_t>(key.isVulkan ? 1 : 0));

			for (auto& d : key.defines)
			{
				HashCombine(h, eastl::hash<eastl::wstring>{}(d.name));
				HashCombine(h, eastl::hash<eastl::wstring>{}(d.value));
			}

			return h;
		}
	};

	winrt::com_ptr<IDxcBlob> GetShader(const wchar_t* FilePath, eastl::vector<DxcDefine> defines = {}, ShaderStage stage = ShaderStage::Library, const wchar_t* EntryPoint = L"Main");
	winrt::com_ptr<IDxcBlob> GetShader(const wchar_t* FilePath, eastl::vector<DxcDefine> defines, const wchar_t* Target, const wchar_t* EntryPoint = L"Main");
};