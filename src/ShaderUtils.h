#pragma once

#include <windows.h>
#include <dxcapi.h>

#include "Types/ShaderDefine.h"
#include "Types/ShaderStage.h"

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
			HMODULE dxcompiler = LoadLibraryW(PLUGIN_FOLDER_W L"/dxcompiler.dll");

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

	bool PreprocessShader(winrt::com_ptr<IDxcBlobUtf8>& outPreprocessedText, const wchar_t* FilePath, eastl::vector<DxcDefine> defines = {}, ShaderStage stage = ShaderStage::Library, const wchar_t* EntryPoint = L"Main");
	bool PreprocessShader(winrt::com_ptr<IDxcBlobUtf8>& outPreprocessedText, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, const wchar_t* Target, const wchar_t* EntryPoint = L"Main");

	void CompileShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines = {}, ShaderStage stage = ShaderStage::Library, const wchar_t* EntryPoint = L"Main");
	void CompileShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, const wchar_t* Target, const wchar_t* EntryPoint = L"Main");

	void CompileRawShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines = {}, ShaderStage stage = ShaderStage::Library, const wchar_t* EntryPoint = L"Main");
	void CompileRawShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, const wchar_t* Target, const wchar_t* EntryPoint = L"Main");

	void CompilePreprocessedShader(winrt::com_ptr<IDxcBlob>& shader, const char* sourceData, size_t sourceSize, const wchar_t* FilePath, ShaderStage stage = ShaderStage::Library, const wchar_t* EntryPoint = L"Main");
	void CompilePreprocessedShader(winrt::com_ptr<IDxcBlob>& shader, const char* sourceData, size_t sourceSize, const wchar_t* FilePath, const wchar_t* Target, const wchar_t* EntryPoint = L"Main");
	
	nvrhi::ShaderLibraryHandle CompileShaderLibrary(nvrhi::IDevice* device, const wchar_t* filePath, const eastl::vector<ShaderDefine>& defines);

	nvrhi::ShaderLibraryHandle CompileShaderLibrary(nvrhi::IDevice* device, const wchar_t* filePath, const eastl::vector<DxcDefine>& defines);
};