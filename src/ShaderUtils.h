#pragma once

#include <windows.h>
#include <dxcapi.h>

#include "Types/ShaderDefine.h"

namespace ShaderUtils
{
	struct DirectXShaderCompiler {
		static DirectXShaderCompiler* GetSingleton()
		{
			static DirectXShaderCompiler singleton;
			return &singleton;
		}

		winrt::com_ptr<IDxcUtils> utils;
		winrt::com_ptr<IDxcCompiler3> compiler;
		winrt::com_ptr<IDxcIncludeHandler> includeHandler;

		using DxcCreateInstanceFn = decltype(&DxcCreateInstance);
		DxcCreateInstanceFn DxcCreateInstance = nullptr;

		DirectXShaderCompiler() {
			HMODULE dxcompiler = LoadLibraryW(L"Data/SKSE/Plugins/dxcompiler.dll");

			if (!dxcompiler) {
				logger::critical("DirectXShaderCompiler - Make sure 'dxcompiler.dll' is placed alongside this plugin's .dll");
				return;
			}

			DxcCreateInstance = reinterpret_cast<DxcCreateInstanceFn>(GetProcAddress(dxcompiler, "DxcCreateInstance"));

			if (!DxcCreateInstance) {
				logger::critical("DirectXShaderCompiler - DxcCreateInstance not found");
				return;
			}

			if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))) {
				logger::critical("Failed to create DxcUtils");
				return;
			}

			if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
				logger::critical("Failed to create DxcCompiler");
				return;
			}

			if (FAILED(utils->CreateDefaultIncludeHandler(includeHandler.put()))) {
				logger::critical("Failed to create Include Handler");
				return;
			}
		}
	};

	void CompileShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines = {}, const wchar_t* Target = L"lib_6_5", const wchar_t* EntryPoint = L"Main");
	
	nvrhi::ShaderLibraryHandle CompileShaderLibrary(nvrhi::IDevice* device, const wchar_t* filePath, const eastl::vector<ShaderDefine>& defines);

	nvrhi::ShaderLibraryHandle CompileShaderLibrary(nvrhi::IDevice* device, const wchar_t* filePath, const eastl::vector<DxcDefine>& defines);
};