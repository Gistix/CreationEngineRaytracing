#include <Windows.h>
#include <d3d12.h>
#include <winrt/base.h>
#include "AgilitySDK.h"
#include "Util.h"

extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = 619;
__declspec(dllexport) extern const char* D3D12SDKPath = ".\\" PLUGIN_FOLDER "\\";
}

#ifndef __ID3D12SDKConfiguration_INTERFACE_DEFINED__
#define __ID3D12SDKConfiguration_INTERFACE_DEFINED__

static constexpr GUID CLSID_D3D12SDKConfiguration = { 0x7cda6aa2, 0xa4e4, 0x4e60, { 0x8a, 0x5c, 0x02, 0xff, 0xb4, 0xd0, 0x0b, 0x7a } };
static constexpr GUID IID_ID3D12SDKConfiguration = { 0x78dbf870, 0xe7ff, 0x4213, { 0xa3, 0x6b, 0x6e, 0x2e, 0x59, 0x01, 0x82, 0x28 } };

MIDL_INTERFACE("78dbf870-e7ff-4213-a36b-6e2e59018228")
ID3D12SDKConfiguration : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE SetSDKVersion(
		UINT SDKVersion,
		_In_z_ PCSTR SDKPath) = 0;
};
#endif

namespace AgilitySDK
{
	bool Initialize()
	{
		logger::info("Agility SDK: Initializing runtime configuration (SDKVersion={}, SDKPath='{}')...", D3D12SDKVersion, D3D12SDKPath);

		// Check if D3D12Core.dll exists at expected relative path
		std::string coreDllPath = std::string(D3D12SDKPath) + "D3D12Core.dll";
		DWORD fileAttrib = GetFileAttributesA(coreDllPath.c_str());
		if (fileAttrib == INVALID_FILE_ATTRIBUTES || (fileAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
			logger::error("Agility SDK: D3D12Core.dll NOT found at path '{}'!", coreDllPath);
		} else {
			logger::info("Agility SDK: Found D3D12Core.dll at '{}'.", coreDllPath);
		}

		HMODULE hD3D12 = GetModuleHandleW(L"d3d12.dll");
		if (!hD3D12) {
			hD3D12 = LoadLibraryW(L"d3d12.dll");
		}
		if (!hD3D12) {
			logger::error("Agility SDK: Failed to load system d3d12.dll (GetLastError: 0x{:08X})", GetLastError());
			return false;
		}

		typedef HRESULT(WINAPI* PFN_D3D12GetInterface)(REFCLSID, REFIID, void**);
		auto pfnD3D12GetInterface = reinterpret_cast<PFN_D3D12GetInterface>(GetProcAddress(hD3D12, "D3D12GetInterface"));
		if (!pfnD3D12GetInterface) {
			logger::error("Agility SDK: d3d12.dll does not export D3D12GetInterface (Agility SDK configuration unavailable on this OS).");
			return false;
		}

		winrt::com_ptr<ID3D12SDKConfiguration> sdkConfig;
		HRESULT hr = pfnD3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(sdkConfig.put()));
		if (FAILED(hr) || !sdkConfig) {
			logger::error("Agility SDK: D3D12GetInterface(CLSID_D3D12SDKConfiguration) failed with HR: 0x{:08X}", static_cast<uint32_t>(hr));
			return false;
		}

		hr = sdkConfig->SetSDKVersion(D3D12SDKVersion, D3D12SDKPath);
		if (FAILED(hr)) {
			logger::error("Agility SDK: SetSDKVersion({}, '{}') failed with HR: 0x{:08X}", D3D12SDKVersion, D3D12SDKPath, static_cast<uint32_t>(hr));
			return false;
		}

		logger::info("Agility SDK: Successfully configured Agility SDK (v{}) at '{}'!", D3D12SDKVersion, D3D12SDKPath);
		return true;
	}
}
