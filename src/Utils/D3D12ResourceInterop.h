#pragma once

#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <unknwn.h>
#include <winrt/base.h>

#ifndef DEFINE_GUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	EXTERN_C const GUID DECLSPEC_SELECTANY name \
		= { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }
#endif

// {318536F9-03B7-4F4C-88EB-116E10A26E85}
DEFINE_GUID(IID_IDXGId3d12InteropDevice,
	0x318536f9, 0x03b7, 0x4f4c, 0x88, 0xeb, 0x11, 0x6e, 0x10, 0xa2, 0x6e, 0x85);

// {0C44365B-E9B4-4B55-A226-C222FFC320D1}
DEFINE_GUID(IID_IDXGId3d12InteropSurface,
	0x0c44365b, 0xe9b4, 0x4b55, 0xa2, 0x26, 0xc2, 0x22, 0xff, 0xc3, 0x20, 0xd1);

// {8E0BB8B3-3DF8-4394-BB9E-324C52F67232}
DEFINE_GUID(IID_IDXGId3d12InteropBuffer,
	0x8e0bb8b3, 0x3df8, 0x4394, 0xbb, 0x9e, 0x32, 0x4c, 0x52, 0xf6, 0x72, 0x32);

// {164B3EF3-401C-4B53-B31C-3B10B0E5D625}
DEFINE_GUID(IID_IDXGId3d12InteropResource,
	0x164b3ef3, 0x401c, 0x4b53, 0xb3, 0x1c, 0x3b, 0x10, 0xb0, 0xe5, 0xd6, 0x25);

// Device interop (matches DXVK's IDXGIVkInteropDevice pattern)
MIDL_INTERFACE("318536f9-03b7-4f4c-88eb-116e10a26e85")
IDXGId3d12InteropDevice : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(
		ID3D12Device** ppDevice) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetCommandQueue(
		ID3D12CommandQueue** ppQueue) = 0;
};

// Surface interop (Texture1D, Texture2D, Texture3D, RenderTargets, BackBuffers)
// Matches DXVK's IDXGIVkInteropSurface pattern
MIDL_INTERFACE("0c44365b-e9b4-4b55-a226-c222ffc320d1")
IDXGId3d12InteropSurface : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(
		ID3D12Device** ppDevice) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetD3D12Resource(
		REFIID riid,
		void** ppvResource) = 0;
};

// Buffer interop (constant buffers, vertex/index buffers, structured buffers)
// Matches DXVK's proposed IDXGIVkInteropBuffer pattern
MIDL_INTERFACE("8e0bb8b3-3df8-4394-bb9e-324c52f67232")
IDXGId3d12InteropBuffer : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(
		ID3D12Device** ppDevice) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetD3D12Resource(
		REFIID riid,
		void** ppvResource) = 0;

	virtual UINT64 STDMETHODCALLTYPE GetSuballocationOffset() = 0;

	virtual D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGpuVirtualAddress() = 0;
};

// Unified resource interop (works on both textures and buffers)
MIDL_INTERFACE("164b3ef3-401c-4b53-b31c-3b10b0e5d625")
IDXGId3d12InteropResource : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(
		ID3D12Device** ppDevice) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetD3D12Resource(
		REFIID riid,
		void** ppvResource) = 0;

	virtual UINT64 STDMETHODCALLTYPE GetSuballocationOffset() = 0;

	virtual D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGpuVirtualAddress() = 0;
};

namespace D3D12ResourceInterop
{
	inline void EnsureResident(ID3D12Resource* a_resource)
	{
		if (!a_resource)
			return;

		winrt::com_ptr<ID3D12Device> dev;
		if (SUCCEEDED(a_resource->GetDevice(IID_PPV_ARGS(dev.put())))) {
			ID3D12Pageable* pageable = a_resource;
			dev->MakeResident(1, &pageable);
		}
	}

	inline winrt::com_ptr<ID3D12Resource> GetD3D12Texture(ID3D11Resource* a_texture)
	{
		if (!a_texture)
			return nullptr;

		// Option A: DXVK-style surface interface
		winrt::com_ptr<IDXGId3d12InteropSurface> interopSurf;
		if (SUCCEEDED(a_texture->QueryInterface(IID_PPV_ARGS(interopSurf.put())))) {
			winrt::com_ptr<ID3D12Resource> d12Res;
			if (SUCCEEDED(interopSurf->GetD3D12Resource(IID_PPV_ARGS(d12Res.put())))) {
				EnsureResident(d12Res.get());
				return d12Res;
			}
		}

		// Unified resource interface
		winrt::com_ptr<IDXGId3d12InteropResource> interopRes;
		if (SUCCEEDED(a_texture->QueryInterface(IID_PPV_ARGS(interopRes.put())))) {
			winrt::com_ptr<ID3D12Resource> d12Res;
			if (SUCCEEDED(interopRes->GetD3D12Resource(IID_PPV_ARGS(d12Res.put())))) {
				EnsureResident(d12Res.get());
				return d12Res;
			}
		}

		// Option B: Direct QueryInterface shortcut
		winrt::com_ptr<ID3D12Resource> d12Res;
		if (SUCCEEDED(a_texture->QueryInterface(IID_PPV_ARGS(d12Res.put())))) {
			EnsureResident(d12Res.get());
			return d12Res;
		}

		return nullptr;
	}

	inline winrt::com_ptr<ID3D12Resource> GetD3D12Buffer(
		ID3D11Resource* a_buffer,
		UINT64* a_suballocationOffset = nullptr,
		D3D12_GPU_VIRTUAL_ADDRESS* a_gpuVA = nullptr)
	{
		if (a_suballocationOffset)
			*a_suballocationOffset = 0;
		if (a_gpuVA)
			*a_gpuVA = 0;

		if (!a_buffer)
			return nullptr;

		// Option A: DXVK-style buffer interface
		winrt::com_ptr<IDXGId3d12InteropBuffer> interopBuf;
		if (SUCCEEDED(a_buffer->QueryInterface(IID_PPV_ARGS(interopBuf.put())))) {
			winrt::com_ptr<ID3D12Resource> d12Res;
			if (SUCCEEDED(interopBuf->GetD3D12Resource(IID_PPV_ARGS(d12Res.put())))) {
				if (a_suballocationOffset)
					*a_suballocationOffset = interopBuf->GetSuballocationOffset();
				if (a_gpuVA)
					*a_gpuVA = interopBuf->GetGpuVirtualAddress();
				EnsureResident(d12Res.get());
				return d12Res;
			}
		}

		// Unified resource interface
		winrt::com_ptr<IDXGId3d12InteropResource> interopRes;
		if (SUCCEEDED(a_buffer->QueryInterface(IID_PPV_ARGS(interopRes.put())))) {
			winrt::com_ptr<ID3D12Resource> d12Res;
			if (SUCCEEDED(interopRes->GetD3D12Resource(IID_PPV_ARGS(d12Res.put())))) {
				if (a_suballocationOffset)
					*a_suballocationOffset = interopRes->GetSuballocationOffset();
				if (a_gpuVA)
					*a_gpuVA = interopRes->GetGpuVirtualAddress();
				EnsureResident(d12Res.get());
				return d12Res;
			}
		}

		// Option B: Direct QueryInterface shortcut
		winrt::com_ptr<ID3D12Resource> d12Res;
		if (SUCCEEDED(a_buffer->QueryInterface(IID_PPV_ARGS(d12Res.put())))) {
			EnsureResident(d12Res.get());
			return d12Res;
		}

		return nullptr;
	}

	inline winrt::com_ptr<ID3D12Device5> GetD3D12Device(ID3D11Device* a_device)
	{
		if (!a_device)
			return nullptr;

		// Option 1: Direct COM QueryInterface
		winrt::com_ptr<ID3D12Device5> d12Dev;
		if (SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(d12Dev.put())))) {
			return d12Dev;
		}

		// Option 2: DXVK-Style Device Interop
		winrt::com_ptr<IDXGId3d12InteropDevice> interopDevice;
		if (SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(interopDevice.put())))) {
			winrt::com_ptr<ID3D12Device> baseDev;
			if (SUCCEEDED(interopDevice->GetDevice(baseDev.put()))) {
				if (SUCCEEDED(baseDev->QueryInterface(IID_PPV_ARGS(d12Dev.put()))))
					return d12Dev;
			}
		}

		// Option 3: Standard Windows ID3D11On12Device1 Interface
		winrt::com_ptr<ID3D11On12Device1> on12Device;
		if (SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(on12Device.put())))) {
			winrt::com_ptr<ID3D12Device> baseDev;
			if (SUCCEEDED(on12Device->GetD3D12Device(IID_PPV_ARGS(baseDev.put())))) {
				if (SUCCEEDED(baseDev->QueryInterface(IID_PPV_ARGS(d12Dev.put()))))
					return d12Dev;
			}
		}

		return nullptr;
	}

	inline winrt::com_ptr<ID3D12CommandQueue> GetD3D12CommandQueue(ID3D11Device* a_device)
	{
		if (!a_device)
			return nullptr;

		// Option 1: Direct COM QueryInterface
		winrt::com_ptr<ID3D12CommandQueue> d12Queue;
		if (SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(d12Queue.put())))) {
			return d12Queue;
		}

		// Option 2: DXVK-Style Device Interop
		winrt::com_ptr<IDXGId3d12InteropDevice> interopDevice;
		if (SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(interopDevice.put())))) {
			if (SUCCEEDED(interopDevice->GetCommandQueue(d12Queue.put()))) {
				return d12Queue;
			}
		}

		return nullptr;
	}
}
