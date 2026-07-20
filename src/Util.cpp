#pragma once

#include "Util.h"
#include "Constants.h"
#include "Renderer.h"

namespace Util
{
	bool IsPlayerFormID(RE::FormID formID)
	{
		return formID == Constants::PLAYER_REFR_FORMID;
	};

	bool IsPlayer(RE::TESForm* form)
	{
		return IsPlayerFormID(form->GetFormID());
	};

	std::string WStringToString(const std::wstring& wideString)
	{
		std::string result;
		std::transform(wideString.begin(), wideString.end(), std::back_inserter(result), [](wchar_t c) {
			return (char)c;
			});
		return result;
	}

	std::wstring StringToWString(const std::string& str)
	{
		if (str.empty())
			return std::wstring();

		int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
			(int)str.size(), nullptr, 0);
		std::wstring wstr(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
			(int)str.size(), &wstr[0], size_needed);
		return wstr;
	}

	eastl::wstring StringToWString(const eastl::string& str)
	{
		if (str.empty())
			return eastl::wstring();

		int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
			(int)str.size(), nullptr, 0);
		eastl::wstring wstr(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
			(int)str.size(), &wstr[0], size_needed);
		return wstr;
	}


	int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
	{
		const float basePhaseCount = 8.0f;
		const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
		return jitterPhaseCount;
	}

	// Calculate halton number for index and base.
	float Halton(int32_t index, int32_t base)
	{
		float f = 1.0f, result = 0.0f;

		for (int32_t currentIndex = index; currentIndex > 0;) {
			f /= (float)base;
			result = result + f * (float)(currentIndex % base);
			currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
		}

		return result;
	}

	void GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
	{
		const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
		const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

		*outX = x;
		*outY = y;
	}

	std::string Format(float3x4 matrix)
	{
		return std::format("{}, {}, {}", matrix.m[0], matrix.m[1], matrix.m[2]);
	}

	std::string Format(float4x4 matrix)
	{
		return std::format("{}, {}, {}, {}", matrix.m[0], matrix.m[1], matrix.m[2], matrix.m[3]);
	}

	void CreateSharedBuffer(ID3D11Buffer* d3d11Buffer, ID3D12Resource** d3d12Buffer)
	{
		if (!d3d11Buffer) {
			logger::error("CreateSharedBuffer - D3D11 buffer is nullptr");
			return;
		}

		// Get underlying resource
		winrt::com_ptr<IDXGIResource1> dxgiResource;
		auto hr = d3d11Buffer->QueryInterface(IID_PPV_ARGS(dxgiResource.put()));

		if (FAILED(hr))
			logger::error("CreateSharedBuffer - QueryInterface failed with hr: 0x{:08X}", hr);

		// Get shared handle from D3D11 texture to enable D3D12 access
		HANDLE sharedHandle = nullptr;
		hr = dxgiResource->GetSharedHandle(&sharedHandle);

		if (FAILED(hr))
			logger::error("CreateSharedBuffer - GetSharedHandle failed with hr: 0x{:08X}", hr);

		// Open the shared D3D11 texture as D3D12 resource
		hr = Renderer::GetNativeD3D12Device()->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(d3d12Buffer));

		if (FAILED(hr))
			logger::error("CreateSharedBuffer - OpenSharedHandle failed with hr: 0x{:08X}", hr);
	};

	void CreateSharedBuffer(RE::ID3D11Buffer* d3d11Buffer, ID3D12Resource** d3d12Buffer)
	{		
		CreateSharedBuffer(reinterpret_cast<ID3D11Buffer*>(d3d11Buffer), d3d12Buffer);
	};
}