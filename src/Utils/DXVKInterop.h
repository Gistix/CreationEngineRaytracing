#pragma once

#include <d3d11.h>
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
