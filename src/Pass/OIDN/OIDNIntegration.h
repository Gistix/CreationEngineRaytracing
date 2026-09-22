#pragma once

#include <PCH.h>
#include "Pass/RenderPass.h"

#if defined(ENABLE_OIDN)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4201 4458)
#endif
#include <OpenImageDenoise/oidn.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <nvrhi/vulkan.h>
#endif

namespace Pass::OIDN
{
	class OIDNIntegration : public RenderPass
	{
	private:
#if defined(ENABLE_OIDN)
		oidn::DeviceRef m_Device;
		oidn::FilterRef m_Filter;

		// OIDN external buffers wrapping NVRHI shared memory in GPU VRAM
		oidn::BufferRef m_ColorOidnBuffer;
		oidn::BufferRef m_AlbedoOidnBuffer;
		oidn::BufferRef m_NormalOidnBuffer;
		oidn::BufferRef m_OutputOidnBuffer;

		// Timeline GPU semaphores for zero-CPU-wait GPU queue fencing
		VkSemaphore m_VkSemVulkanToOIDN = VK_NULL_HANDLE;
		VkSemaphore m_VkSemOIDNToVulkan = VK_NULL_HANDLE;
		oidn::SemaphoreRef m_OidnSemVulkanToOIDN;
		oidn::SemaphoreRef m_OidnSemOIDNToVulkan;
		uint64_t m_TimelineValue = 0;
		bool m_SemaphoresInitialized = false;
		PFN_vkDestroySemaphore m_pfnDestroySemaphore = nullptr;

		void CreateSemaphores();
		void DestroySemaphores();
#endif

		// NVRHI shared GPU buffers backing the OIDN inputs/output
		nvrhi::BufferHandle m_ColorSharedBuffer;
		nvrhi::BufferHandle m_AlbedoSharedBuffer;
		nvrhi::BufferHandle m_NormalSharedBuffer;
		nvrhi::BufferHandle m_OutputSharedBuffer;

		// Pipelines for pre/post processing
		nvrhi::ShaderHandle m_PrepareShader;
		nvrhi::ComputePipelineHandle m_PreparePipeline;
		nvrhi::BindingLayoutHandle m_PrepareBindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_PrepareBindingSets;

		nvrhi::ShaderHandle m_CompositeShader;
		nvrhi::ComputePipelineHandle m_CompositePipeline;
		nvrhi::BindingLayoutHandle m_CompositeBindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_CompositeBindingSets;

		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetsDirty{};

		uint2 m_CurrentResolution = { 0, 0 };
		bool m_ResourcesDirty = true;
		bool m_DeviceInitialized = false;

		void InitializeOIDNDevice();
		void CreatePipelines();
		void CreateResources();
		void CheckBindings();

	public:
		OIDNIntegration(Renderer* renderer);
		~OIDNIntegration() override;

		void Initialize() override;
		void SettingsChanged(const Settings& settings) override;
		void ResolutionChanged(uint2 resolution) override;
		void Execute(nvrhi::ICommandList* commandList) override;
		void ReloadShaders() override;
	};
}
