#pragma once

#include "ShaderCache.h"
#include "ShaderUtils.h"
#include "Util.h"
#include <dxcapi.h>
#include <shlwapi.h>

namespace ShaderCache
{
	eastl::unordered_map<ShaderKey, winrt::com_ptr<IDxcBlob>, ShaderKeyHash> m_Shaders;

	IDxcBlob* GetShader(const wchar_t* filePath, eastl::vector<DxcDefine> defines, const wchar_t* target, const wchar_t* entryPoint)
	{
		ShaderKey shaderKey(filePath, defines, target, entryPoint);

		// Return cached shader
		if (auto it = m_Shaders.find(shaderKey); it != m_Shaders.end()) {
			logger::info("ShaderCache::GetShader - Returning cached shader");
			return it->second.get();
		}

		winrt::com_ptr<IDxcBlob> blob;

		ShaderUtils::CompileShader(blob, filePath, defines, target, entryPoint);

		// Save shader to cache
		auto [it, emplaced] = m_Shaders.try_emplace(shaderKey, std::move(blob));

		if (!emplaced) {
			logger::error("ShaderCache::GetShader - Emplace failed.");
			return nullptr;
		}

		logger::info("ShaderCache::GetShader - Returning compiled shader");

		return it->second.get();
	}
};