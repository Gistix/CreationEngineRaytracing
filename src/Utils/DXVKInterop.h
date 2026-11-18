#pragma once

#include <d3d11_4.h>
#include <vulkan/vulkan.h>

struct IDXGIVkInteropDevice;

MIDL_INTERFACE("5546cf8c-77e7-4341-b05d-8d4d5000e77d")
IDXGIVkInteropSurface : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(IDXGIVkInteropDevice** ppDevice) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(
		VkImage* pHandle,
		VkImageLayout* pLayout,
		VkImageCreateInfo* pInfo) = 0;
};

MIDL_INTERFACE("b7b13df1-5364-4e94-81d3-6e3e5c9f91a0")
IDXGIVkInteropBuffer : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(IDXGIVkInteropDevice** ppDevice) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVulkanBufferInfo(
		VkBuffer* pBuffer,
		VkDeviceSize* pOffset,
		VkDeviceSize* pLength,
		VkDeviceAddress* pGpuAddress) = 0;
};

MIDL_INTERFACE("e2ef5fa5-dc21-4af7-90c4-f67ef6a09323")
IDXGIVkInteropDevice : public IUnknown
{
	virtual void STDMETHODCALLTYPE GetVulkanHandles(
		VkInstance* pInstance,
		VkPhysicalDevice* pPhysDev,
		VkDevice* pDevice) = 0;
	virtual void STDMETHODCALLTYPE GetSubmissionQueue(
		VkQueue* pQueue,
		uint32_t* pQueueFamilyIndex) = 0;
	virtual void STDMETHODCALLTYPE TransitionSurfaceLayout(
		IDXGIVkInteropSurface* pSurface,
		const VkImageSubresourceRange* pSubresources,
		VkImageLayout OldLayout,
		VkImageLayout NewLayout) = 0;
	virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;
	virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;
	virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;
};

struct D3D11_TEXTURE2D_DESC1;

MIDL_INTERFACE("e2ef5fa5-dc21-4af7-90c4-f67ef6a09324")
IDXGIVkInteropDevice1 : public IDXGIVkInteropDevice
{
	virtual void STDMETHODCALLTYPE GetSubmissionQueue1(
		VkQueue* pQueue,
		uint32_t* pQueueIndex,
		uint32_t* pQueueFamilyIndex) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateTexture2DFromVkImage(
		const D3D11_TEXTURE2D_DESC1* pDesc,
		VkImage vkImage,
		ID3D11Texture2D** ppTexture2D) = 0;
};

// Community Shaders DXVK fork only: a queue reserved for external renderers,
// and the timeline semaphore behind each ID3D11Fence.
MIDL_INTERFACE("e2ef5fa5-dc21-4af7-90c4-f67ef6a09325")
IDXGIVkInteropDevice2 : public IDXGIVkInteropDevice1
{
	virtual HRESULT STDMETHODCALLTYPE GetExternalSubmissionQueue(
		UINT Index,
		VkQueue* pQueue,
		uint32_t* pQueueIndex,
		uint32_t* pQueueFamilyIndex) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFenceSemaphore(
		ID3D11Fence* pFence,
		VkSemaphore* pSemaphore) = 0;
};
