#pragma once

#include <dxcapi.h>

#include "Types/ShaderDefine.h"

namespace ShaderCache
{
	struct ShaderKey {
		eastl::wstring filePath; 
		eastl::vector<ShaderDefine> defines; 
		eastl::wstring target;
		eastl::wstring entryPoint;

		ShaderKey(const wchar_t* a_filePath, eastl::vector<DxcDefine> a_defines, const wchar_t* a_target, const wchar_t* a_entryPoint) 
			: filePath(a_filePath), target(a_target), entryPoint(a_entryPoint)
		{
			defines.reserve(a_defines.size());

			for (auto& define: a_defines)
			{
				defines.emplace_back(define.Name, define.Value);
			}
		}

		bool operator==(const ShaderKey& other) const
		{
			return filePath == other.filePath &&
				target == other.target &&
				entryPoint == other.entryPoint &&
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

			for (auto& d : key.defines)
			{
				HashCombine(h, eastl::hash<eastl::wstring>{}(d.name));
				HashCombine(h, eastl::hash<eastl::wstring>{}(d.value));
			}

			return h;
		}
	};

	IDxcBlob* GetShader(const wchar_t* FilePath, eastl::vector<DxcDefine> defines = {}, const wchar_t* Target = L"lib_6_5", const wchar_t* EntryPoint = L"Main");
};