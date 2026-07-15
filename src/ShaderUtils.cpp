#pragma once

#include "ShaderUtils.h"
#include "Util.h"
#include <shlwapi.h>

#include "ShaderCache.h"

namespace ShaderUtils
{
	void CompileShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, const wchar_t* Target, const wchar_t* EntryPoint)
	{
		auto dxc = DirectXShaderCompiler::GetSingleton();

		winrt::com_ptr<IDxcBlobEncoding> source;
		if (FAILED(dxc->utils->LoadFile(FilePath, nullptr, source.put()))) {
			std::string str = Util::WStringToString(FilePath);
			logger::error("Failed to load shader file, check if {} exists.", str);
			return;
		}

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = source->GetBufferPointer();
		sourceBuffer.Size = source->GetBufferSize();
		sourceBuffer.Encoding = DXC_CP_ACP;

		eastl::vector<LPCWSTR> args = {
			FilePath,
			L"-E",  EntryPoint,
			L"-enable-16bit-types",
			L"-T", Target,
			L"-I", L"Data/shaders",
			L"-I", L"extern/RTXDI-Library/Include",
			L"-O3"
		};

#ifndef NDEBUG
		// Embedded source/debug information substantially increases DXC work and
		// shader-library size. Keep it for development builds only.
		args.insert(args.end(), { L"-Zi", L"-Zss", L"-Qembed_debug" });
#endif

		winrt::com_ptr<IDxcCompilerArgs> compilerArgs;
		dxc->DxcCreateInstance(CLSID_DxcCompilerArgs, IID_PPV_ARGS(&compilerArgs));

		compilerArgs->AddArguments(args.data(), static_cast<uint>(args.size()));

#if defined(SKYRIM)
		defines.emplace_back(L"SKYRIM", L"");
#elif defined(FALLOUT4)
		defines.emplace_back(L"FALLOUT4", L"");
#endif

		// Means the game version has been defined
		defines.emplace_back(L"GAME_DEF", L"");

		compilerArgs->AddDefines(defines.data(), static_cast<uint>(defines.size()));

		winrt::com_ptr<IDxcResult> result;
		if (FAILED(dxc->compiler->Compile(&sourceBuffer, compilerArgs->GetArguments(), compilerArgs->GetCount(), dxc->includeHandler.get(), IID_PPV_ARGS(&result)))) {
			logger::error("Compile call failed");
			return;
		}

		winrt::com_ptr<IDxcBlobUtf8> errors;
		if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr))) {
			if (errors && errors->GetStringLength() > 0) {
				logger::error("Shader compilation errors: {}", errors->GetStringPointer());
			}
		} else {
			logger::error("Failed to get compilation errors");
			return;
		}

		if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr))) {
			logger::error("Failed to get compiled shader");
			return;
		}
	}

	nvrhi::ShaderLibraryHandle CompileShaderLibrary(nvrhi::IDevice* device, const wchar_t* filePath, const eastl::vector<DxcDefine>& defines)
	{
		IDxcBlob* blob = ShaderCache::GetShader(filePath, defines);

		if (!blob)
			return nullptr;

		return device->createShaderLibrary(blob->GetBufferPointer(), blob->GetBufferSize());
	}

	nvrhi::ShaderLibraryHandle CompileShaderLibrary(nvrhi::IDevice* device, const wchar_t* filePath, const eastl::vector<ShaderDefine>& defines)
	{
		auto numDefines = defines.size();

		eastl::vector<DxcDefine> dxcDefines(numDefines);

		for (size_t i = 0; i < numDefines; i++)
		{
			auto& define = defines[i];
			dxcDefines[i] = { define.name.c_str(), define.value.c_str() };
		}

		return CompileShaderLibrary(device, filePath, dxcDefines);
	}
};
