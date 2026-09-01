#include "ShaderUtils.h"
#include "Util.h"
#include "Renderer.h"
#include <shlwapi.h>

#include "ShaderCache.h"

namespace ShaderUtils
{
	bool PreprocessShader(winrt::com_ptr<IDxcBlobUtf8>& outPreprocessedText, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, ShaderStage stage, const wchar_t* EntryPoint)
	{
		const std::wstring target = Renderer::GetSingleton()->GetShaderTarget(stage);
		return PreprocessShader(outPreprocessedText, FilePath, std::move(defines), target.c_str(), EntryPoint);
	}

	bool PreprocessShader(winrt::com_ptr<IDxcBlobUtf8>& outPreprocessedText, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, const wchar_t* Target, const wchar_t* EntryPoint)
	{
		auto dxc = DirectXShaderCompiler::GetSingleton();

		winrt::com_ptr<IDxcBlobEncoding> source;
		if (FAILED(dxc->utils->LoadFile(FilePath, nullptr, source.put()))) {
			std::string str = Util::WStringToString(FilePath);
			logger::error("PreprocessShader - Failed to load shader file, check if {} exists.", str);
			return false;
		}

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = source->GetBufferPointer();
		sourceBuffer.Size = source->GetBufferSize();
		sourceBuffer.Encoding = DXC_CP_ACP;

		eastl::vector<LPCWSTR> args = {
			FilePath,
			L"-P",
			L"-E",  EntryPoint,
			L"-enable-16bit-types",
			L"-T", Target,
			L"-I", L"Data/shaders",
			L"-I", L"extern/RTXDI-Library/Include"
		};

		if (Renderer::GetSingleton()->IsVulkan()) {
			args.insert(args.end(), {
				L"-spirv",
				L"-fspv-target-env=vulkan1.2",
				L"-fvk-use-dx-layout"
			});
		}

		winrt::com_ptr<IDxcCompilerArgs> compilerArgs;
		dxc->DxcCreateInstance(CLSID_DxcCompilerArgs, IID_PPV_ARGS(&compilerArgs));

		compilerArgs->AddArguments(args.data(), static_cast<uint>(args.size()));

#if defined(SKYRIM)
		defines.emplace_back(L"SKYRIM", L"");
#elif defined(FALLOUT4)
		defines.emplace_back(L"FALLOUT4", L"");
#endif

		defines.emplace_back(L"GAME_DEF", L"");

		compilerArgs->AddDefines(defines.data(), static_cast<uint>(defines.size()));

		winrt::com_ptr<IDxcResult> result;
		if (FAILED(dxc->compiler->Compile(&sourceBuffer, compilerArgs->GetArguments(), compilerArgs->GetCount(), dxc->includeHandler.get(), IID_PPV_ARGS(&result)))) {
			logger::error("PreprocessShader - Compile call failed");
			return false;
		}

		winrt::com_ptr<IDxcBlobUtf8> errors;
		if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr))) {
			if (errors && errors->GetStringLength() > 0) {
				logger::error("Shader preprocess errors: {}", errors->GetStringPointer());
			}
		}

		if (SUCCEEDED(result->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(&outPreprocessedText), nullptr)) && outPreprocessedText && outPreprocessedText->GetStringLength() > 0) {
			return true;
		}

		// Fallback: some DXC versions emit preprocessed text via DXC_OUT_OBJECT
		// rather than DXC_OUT_HLSL when using -P.  This safely retrieves it as
		// UTF-8.  If actual compiled bytecode were returned here (unexpected with
		// -P), the hash would differ from source-level hashes, causing cache
		// misses rather than serving stale bytecode.
		winrt::com_ptr<IDxcBlob> objectBlob;
		if (SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&objectBlob), nullptr)) && objectBlob) {
			if (SUCCEEDED(dxc->utils->GetBlobAsUtf8(objectBlob.get(), outPreprocessedText.put())) && outPreprocessedText) {
				return true;
			}
		}

		logger::error("PreprocessShader - Failed to get preprocessed output");
		return false;
	}

	void CompileShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, ShaderStage stage, const wchar_t* EntryPoint)
	{
		const std::wstring target = Renderer::GetSingleton()->GetShaderTarget(stage);
		CompileShader(shader, FilePath, std::move(defines), target.c_str(), EntryPoint);
	}

	void CompileShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, const wchar_t* Target, const wchar_t* EntryPoint)
	{
		winrt::com_ptr<IDxcBlob> blob = ShaderCache::GetShader(FilePath, std::move(defines), Target, EntryPoint);
		if (blob) {
			shader.copy_from(blob.get());
		} else {
			shader = nullptr;
		}
	}

	void CompileRawShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, ShaderStage stage, const wchar_t* EntryPoint)
	{
		const std::wstring target = Renderer::GetSingleton()->GetShaderTarget(stage);
		CompileRawShader(shader, FilePath, std::move(defines), target.c_str(), EntryPoint);
	}

	void CompileRawShader(winrt::com_ptr<IDxcBlob>& shader, const wchar_t* FilePath, eastl::vector<DxcDefine> defines, const wchar_t* Target, const wchar_t* EntryPoint)
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
			L"-all-resources-bound",
			L"-T", Target,
			L"-I", L"Data/shaders",
			L"-I", L"extern/RTXDI-Library/Include",
			L"-O3"
		};

		if (Renderer::GetSingleton()->IsVulkan()) {
			args.insert(args.end(), {
				L"-spirv",
				L"-fspv-target-env=vulkan1.2",
				L"-fvk-use-dx-layout"
			});
		}

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

	void CompilePreprocessedShader(winrt::com_ptr<IDxcBlob>& shader, const char* sourceData, size_t sourceSize, const wchar_t* FilePath, ShaderStage stage, const wchar_t* EntryPoint)
	{
		const std::wstring target = Renderer::GetSingleton()->GetShaderTarget(stage);
		CompilePreprocessedShader(shader, sourceData, sourceSize, FilePath, target.c_str(), EntryPoint);
	}

	void CompilePreprocessedShader(winrt::com_ptr<IDxcBlob>& shader, const char* sourceData, size_t sourceSize, const wchar_t* FilePath, const wchar_t* Target, const wchar_t* EntryPoint)
	{
		auto dxc = DirectXShaderCompiler::GetSingleton();

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = sourceData;
		sourceBuffer.Size = sourceSize;
		sourceBuffer.Encoding = DXC_CP_UTF8;

		eastl::vector<LPCWSTR> args = {
			FilePath,   // Filename used for diagnostics only
			L"-E",  EntryPoint,
			L"-enable-16bit-types",
			L"-all-resources-bound",
			L"-T", Target,
			L"-O3"
		};

		if (Renderer::GetSingleton()->IsVulkan()) {
			args.insert(args.end(), {
				L"-spirv",
				L"-fspv-target-env=vulkan1.2",
				L"-fvk-use-dx-layout"
			});
		}

#ifndef NDEBUG
		// Embedded source/debug information substantially increases DXC work and
		// shader-library size. Keep it for development builds only.
		args.insert(args.end(), { L"-Zi", L"-Zss", L"-Qembed_debug" });
#endif

		winrt::com_ptr<IDxcCompilerArgs> compilerArgs;
		dxc->DxcCreateInstance(CLSID_DxcCompilerArgs, IID_PPV_ARGS(&compilerArgs));
		compilerArgs->AddArguments(args.data(), static_cast<uint>(args.size()));

		// No defines or include handler needed — source is already fully preprocessed.
		winrt::com_ptr<IDxcResult> result;
		if (FAILED(dxc->compiler->Compile(&sourceBuffer, compilerArgs->GetArguments(), compilerArgs->GetCount(), nullptr, IID_PPV_ARGS(&result)))) {
			logger::error("CompilePreprocessedShader - Compile call failed");
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
		winrt::com_ptr<IDxcBlob> blob = ShaderCache::GetShader(filePath, defines);
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
