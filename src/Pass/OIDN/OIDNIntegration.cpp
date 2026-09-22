#include "OIDNIntegration.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderUtils.h"
#include "Util.h"

namespace Pass::OIDN
{
	OIDNIntegration::OIDNIntegration(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_BindingSetsDirty.fill(true);
	}

	OIDNIntegration::~OIDNIntegration()
	{
#if defined(ENABLE_OIDN)
		DestroySemaphores();
#endif
	}

#if defined(ENABLE_OIDN)
	void OIDNIntegration::CreateSemaphores()
	{
		if (!m_DeviceInitialized || !m_Device)
			return;

		if (!GetRenderer()->IsVulkan())
			return;

		if (m_SemaphoresInitialized)
			return;

		auto* nvrhiDevice = GetRenderer()->GetDevice();
		if (!nvrhiDevice)
			return;

		VkDevice vkDevice = static_cast<VkDevice>(nvrhiDevice->getNativeObject(nvrhi::ObjectTypes::VK_Device));
		if (!vkDevice) {
			logger::warn("OIDNIntegration: Could not retrieve native VkDevice for semaphores");
			return;
		}

		HMODULE hVk = GetModuleHandleA("vulkan-1.dll");
		if (!hVk)
			hVk = LoadLibraryA("vulkan-1.dll");
		if (!hVk) {
			logger::warn("OIDNIntegration: vulkan-1.dll not loaded, falling back to CPU wait");
			return;
		}

		auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
			GetProcAddress(hVk, "vkGetDeviceProcAddr"));
		if (!pfnGetDeviceProcAddr) {
			logger::warn("OIDNIntegration: vkGetDeviceProcAddr not found in vulkan-1.dll");
			return;
		}

		auto pfnCreateSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(
			pfnGetDeviceProcAddr(vkDevice, "vkCreateSemaphore"));
		m_pfnDestroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(
			pfnGetDeviceProcAddr(vkDevice, "vkDestroySemaphore"));
		auto pfnGetSemaphoreWin32HandleKHR = reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
			pfnGetDeviceProcAddr(vkDevice, "vkGetSemaphoreWin32HandleKHR"));

		if (!pfnCreateSemaphore || !m_pfnDestroySemaphore || !pfnGetSemaphoreWin32HandleKHR) {
			logger::warn("OIDNIntegration: Required Vulkan semaphore functions not found, falling back to CPU wait");
			return;
		}

		VkExportSemaphoreCreateInfo exportInfo{};
		exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
		exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

		VkSemaphoreTypeCreateInfo timelineInfo{};
		timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		timelineInfo.pNext = &exportInfo;
		timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		timelineInfo.initialValue = 0;

		VkSemaphoreCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		createInfo.pNext = &timelineInfo;

		if (pfnCreateSemaphore(vkDevice, &createInfo, nullptr, &m_VkSemVulkanToOIDN) != VK_SUCCESS ||
			pfnCreateSemaphore(vkDevice, &createInfo, nullptr, &m_VkSemOIDNToVulkan) != VK_SUCCESS) {
			logger::warn("OIDNIntegration: Failed to create exportable timeline semaphores");
			DestroySemaphores();
			return;
		}

		VkSemaphoreGetWin32HandleInfoKHR getHandleInfo{};
		getHandleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
		getHandleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

		HANDLE h1 = NULL;
		getHandleInfo.semaphore = m_VkSemVulkanToOIDN;
		VkResult r1 = pfnGetSemaphoreWin32HandleKHR(vkDevice, &getHandleInfo, &h1);

		HANDLE h2 = NULL;
		getHandleInfo.semaphore = m_VkSemOIDNToVulkan;
		VkResult r2 = pfnGetSemaphoreWin32HandleKHR(vkDevice, &getHandleInfo, &h2);

		if (r1 != VK_SUCCESS || r2 != VK_SUCCESS || !h1 || !h2) {
			logger::warn("OIDNIntegration: Failed to export Win32 handles for timeline semaphores (r1={}, r2={})", static_cast<int>(r1), static_cast<int>(r2));
			if (h1) CloseHandle(h1);
			if (h2) CloseHandle(h2);
			DestroySemaphores();
			return;
		}

		try {
			m_OidnSemVulkanToOIDN = m_Device.newSemaphore(
				oidn::ExternalSemaphoreTypeFlags(oidn::ExternalSemaphoreTypeFlag::TimelineSemaphoreWin32),
				h1,
				nullptr);
			m_OidnSemOIDNToVulkan = m_Device.newSemaphore(
				oidn::ExternalSemaphoreTypeFlags(oidn::ExternalSemaphoreTypeFlag::TimelineSemaphoreWin32),
				h2,
				nullptr);
			CloseHandle(h1);
			CloseHandle(h2);
			m_TimelineValue = 0;
			m_SemaphoresInitialized = true;
			logger::info("OIDNIntegration: Timeline GPU semaphores created and imported successfully (zero CPU wait GPU fencing active)");
		} catch (const std::exception& e) {
			logger::error("OIDNIntegration: Failed to import timeline semaphores into OIDN: {}", e.what());
			CloseHandle(h1);
			CloseHandle(h2);
			DestroySemaphores();
		}
	}

	void OIDNIntegration::DestroySemaphores()
	{
		m_OidnSemVulkanToOIDN = nullptr;
		m_OidnSemOIDNToVulkan = nullptr;
		m_SemaphoresInitialized = false;

		if (GetRenderer()->IsVulkan() && m_pfnDestroySemaphore) {
			auto* nvrhiDevice = GetRenderer()->GetDevice();
			if (nvrhiDevice) {
				VkDevice vkDevice = static_cast<VkDevice>(nvrhiDevice->getNativeObject(nvrhi::ObjectTypes::VK_Device));
				if (vkDevice) {
					if (m_VkSemVulkanToOIDN != VK_NULL_HANDLE) {
						m_pfnDestroySemaphore(vkDevice, m_VkSemVulkanToOIDN, nullptr);
						m_VkSemVulkanToOIDN = VK_NULL_HANDLE;
					}
					if (m_VkSemOIDNToVulkan != VK_NULL_HANDLE) {
						m_pfnDestroySemaphore(vkDevice, m_VkSemOIDNToVulkan, nullptr);
						m_VkSemOIDNToVulkan = VK_NULL_HANDLE;
					}
				}
			}
		}
	}
#endif

	void OIDNIntegration::Initialize()
	{
		InitializeOIDNDevice();
		CreatePipelines();
	}

	void OIDNIntegration::InitializeOIDNDevice()
	{
#if defined(ENABLE_OIDN)
		if (m_DeviceInitialized)
			return;

		SetDllDirectoryW(PLUGIN_FOLDER_W);
		LoadLibraryW(PLUGIN_FOLDER_W L"/OpenImageDenoise_core.dll");
		LoadLibraryW(PLUGIN_FOLDER_W L"/OpenImageDenoise.dll");
		LoadLibraryW(PLUGIN_FOLDER_W L"/OpenImageDenoise_device_cuda.dll");

		try {
			// First try CUDA for NVIDIA GPUs
			m_Device = oidn::newDevice(oidn::DeviceType::CUDA);
		} catch (...) {
			m_Device = nullptr;
		}

		if (!m_Device) {
			try {
				// Next try SYCL (Intel GPUs)
				m_Device = oidn::newDevice(oidn::DeviceType::SYCL);
			} catch (...) {
				m_Device = nullptr;
			}
		}

		if (!m_Device) {
			try {
				// Next try HIP (AMD GPUs)
				m_Device = oidn::newDevice(oidn::DeviceType::HIP);
			} catch (...) {
				m_Device = nullptr;
			}
		}

		if (!m_Device) {
			try {
				// Fall back to default GPU device
				m_Device = oidn::newDevice(oidn::DeviceType::Default);
			} catch (...) {
				m_Device = nullptr;
			}
		}

		if (m_Device) {
			m_Device.setErrorFunction([]([[maybe_unused]] void* userPtr, oidn::Error error, const char* message) {
				logger::error("OIDN error ({}): {}", static_cast<int>(error), message ? message : "<unknown>");
			}, nullptr);

			m_Device.commit();
			m_DeviceInitialized = true;
			logger::info("OIDNIntegration: GPU device successfully initialized");
			CreateSemaphores();
		} else {
			logger::error("OIDNIntegration: Failed to initialize any GPU OIDN device.");
		}
#endif
	}

	void OIDNIntegration::CreatePipelines()
	{
		auto* device = GetRenderer()->GetDevice();

		// Pipeline 1: PrepareOIDNInputs (Unpacks Color, Albedo, Normal to RGBA16_FLOAT linear buffers)
		{
			nvrhi::BindingLayoutDesc layoutDesc;
			layoutDesc.visibility = nvrhi::ShaderType::Compute;
			layoutDesc.bindings = {
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // Camera
				nvrhi::BindingLayoutItem::Texture_SRV(0),            // InputColor
				nvrhi::BindingLayoutItem::Texture_SRV(1),            // InputAlbedo
				nvrhi::BindingLayoutItem::Texture_SRV(2),            // InputNormal
				nvrhi::BindingLayoutItem::TypedBuffer_UAV(0),        // OutputColor
				nvrhi::BindingLayoutItem::TypedBuffer_UAV(1),        // OutputAlbedo
				nvrhi::BindingLayoutItem::TypedBuffer_UAV(2)         // OutputNormal
			};
			m_PrepareBindingLayout = device->createBindingLayout(layoutDesc);

			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/PrepareOIDNInputs.hlsl", {}, ShaderStage::Compute, L"Main");
			if (blob) {
				m_PrepareShader = device->createShader(
					{ nvrhi::ShaderType::Compute, "", "Main" },
					blob->GetBufferPointer(),
					blob->GetBufferSize()
				);

				auto pipelineDesc = nvrhi::ComputePipelineDesc()
					.setComputeShader(m_PrepareShader)
					.addBindingLayout(m_PrepareBindingLayout);

				m_PreparePipeline = device->createComputePipeline(pipelineDesc);
			}
		}

		// Pipeline 2: PostOIDNComposite (Converts denoised linear radiance from buffer to gamma in MainTexture)
		{
			nvrhi::BindingLayoutDesc layoutDesc;
			layoutDesc.visibility = nvrhi::ShaderType::Compute;
			layoutDesc.bindings = {
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // Camera
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // Features
				nvrhi::BindingLayoutItem::TypedBuffer_SRV(0),        // DenoisedLinear
				nvrhi::BindingLayoutItem::Texture_UAV(0)             // MainTexture
			};
			m_CompositeBindingLayout = device->createBindingLayout(layoutDesc);

			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/PostOIDNComposite.hlsl", {}, ShaderStage::Compute, L"Main");
			if (blob) {
				m_CompositeShader = device->createShader(
					{ nvrhi::ShaderType::Compute, "", "Main" },
					blob->GetBufferPointer(),
					blob->GetBufferSize()
				);

				auto pipelineDesc = nvrhi::ComputePipelineDesc()
					.setComputeShader(m_CompositeShader)
					.addBindingLayout(m_CompositeBindingLayout);

				m_CompositePipeline = device->createComputePipeline(pipelineDesc);
			}
		}
	}

	void OIDNIntegration::CreateResources()
	{
		if (m_CurrentResolution.x == 0 || m_CurrentResolution.y == 0)
			return;

		auto* device = GetRenderer()->GetDevice();
		const size_t byteSize = static_cast<size_t>(m_CurrentResolution.x) * m_CurrentResolution.y * 8; // 4 * sizeof(half) = 8

		nvrhi::BufferDesc bufDesc;
		bufDesc.byteSize = byteSize;
		bufDesc.format = nvrhi::Format::RGBA16_FLOAT;
		bufDesc.structStride = 8;
		bufDesc.canHaveUAVs = true;
		bufDesc.canHaveTypedViews = true;
		bufDesc.sharedResourceFlags = nvrhi::SharedResourceFlags::Shared;
		bufDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		bufDesc.keepInitialState = true;

		bufDesc.debugName = "OIDN_ColorSharedBuffer";
		m_ColorSharedBuffer = device->createBuffer(bufDesc);

		bufDesc.debugName = "OIDN_AlbedoSharedBuffer";
		m_AlbedoSharedBuffer = device->createBuffer(bufDesc);

		bufDesc.debugName = "OIDN_NormalSharedBuffer";
		m_NormalSharedBuffer = device->createBuffer(bufDesc);

		bufDesc.debugName = "OIDN_OutputSharedBuffer";
		m_OutputSharedBuffer = device->createBuffer(bufDesc);

#if defined(ENABLE_OIDN)
		if (m_DeviceInitialized && m_Device) {
			void* colorHandle = m_ColorSharedBuffer->getNativeObject(nvrhi::ObjectTypes::SharedHandle);
			void* albedoHandle = m_AlbedoSharedBuffer->getNativeObject(nvrhi::ObjectTypes::SharedHandle);
			void* normalHandle = m_NormalSharedBuffer->getNativeObject(nvrhi::ObjectTypes::SharedHandle);
			void* outputHandle = m_OutputSharedBuffer->getNativeObject(nvrhi::ObjectTypes::SharedHandle);

			if (!colorHandle || !albedoHandle || !normalHandle || !outputHandle) {
				logger::error("OIDNIntegration: Failed to retrieve shared handles for buffers");
				return;
			}

			oidn::ExternalMemoryTypeFlags extType;
			if (GetRenderer()->IsVulkan()) {
				extType = oidn::ExternalMemoryTypeFlags(oidn::ExternalMemoryTypeFlag::OpaqueWin32);
				extType |= oidn::ExternalMemoryTypeFlag::Dedicated;
			} else {
				extType = oidn::ExternalMemoryTypeFlags(oidn::ExternalMemoryTypeFlag::D3D12Resource);
			}

			try {
				m_ColorOidnBuffer = m_Device.newBuffer(extType, colorHandle, nullptr, byteSize);
				m_AlbedoOidnBuffer = m_Device.newBuffer(extType, albedoHandle, nullptr, byteSize);
				m_NormalOidnBuffer = m_Device.newBuffer(extType, normalHandle, nullptr, byteSize);
				m_OutputOidnBuffer = m_Device.newBuffer(extType, outputHandle, nullptr, byteSize);

				m_Filter = m_Device.newFilter("RT");
				const size_t pixelStride = 8;
				const size_t rowStride = static_cast<size_t>(m_CurrentResolution.x) * pixelStride;

				m_Filter.setImage("color", m_ColorOidnBuffer, oidn::Format::Half3, m_CurrentResolution.x, m_CurrentResolution.y, 0, pixelStride, rowStride);
				m_Filter.setImage("albedo", m_AlbedoOidnBuffer, oidn::Format::Half3, m_CurrentResolution.x, m_CurrentResolution.y, 0, pixelStride, rowStride);
				m_Filter.setImage("normal", m_NormalOidnBuffer, oidn::Format::Half3, m_CurrentResolution.x, m_CurrentResolution.y, 0, pixelStride, rowStride);
				m_Filter.setImage("output", m_OutputOidnBuffer, oidn::Format::Half3, m_CurrentResolution.x, m_CurrentResolution.y, 0, pixelStride, rowStride);

				m_Filter.set("hdr", true);
				m_Filter.set("cleanAux", true);
				m_Filter.set("quality", oidn::Quality::Balanced);
				m_Filter.commit();
				logger::info("OIDNIntegration: Shared GPU buffers bound to OIDN successfully (Quality: Balanced)");
			} catch (const std::exception& e) {
				logger::error("OIDNIntegration: Exception setting up shared OIDN buffers: {}", e.what());
			}
		}
		if (!m_SemaphoresInitialized)
			CreateSemaphores();
#endif

		m_BindingSetsDirty.fill(true);
		m_ResourcesDirty = false;
	}

	void OIDNIntegration::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetsDirty[currentSlot] && m_PrepareBindingSets[currentSlot] && m_CompositeBindingSets[currentSlot])
			return;

		auto* renderer = GetRenderer();
		auto* device = renderer->GetDevice();
		auto* scene = Scene::GetSingleton();
		auto& textureManager = renderer->RenderTargetManager();

		auto* diffuseAlbedo = textureManager.GetTexture(RenderTarget::DiffuseAlbedo);
		auto* faceNormals = textureManager.GetTexture(RenderTarget::FaceNormals);
		auto* mainTexture = renderer->GetMainTexture();

		// Binding set for PrepareOIDNInputs
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
				nvrhi::BindingSetItem::Texture_SRV(0, mainTexture),
				nvrhi::BindingSetItem::Texture_SRV(1, diffuseAlbedo),
				nvrhi::BindingSetItem::Texture_SRV(2, faceNormals),
				nvrhi::BindingSetItem::TypedBuffer_UAV(0, m_ColorSharedBuffer, nvrhi::Format::RGBA16_FLOAT),
				nvrhi::BindingSetItem::TypedBuffer_UAV(1, m_AlbedoSharedBuffer, nvrhi::Format::RGBA16_FLOAT),
				nvrhi::BindingSetItem::TypedBuffer_UAV(2, m_NormalSharedBuffer, nvrhi::Format::RGBA16_FLOAT)
			};
			m_PrepareBindingSets[currentSlot] = device->createBindingSet(desc, m_PrepareBindingLayout);
		}

		// Binding set for PostOIDNComposite
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(1, scene->GetFeatureBuffer()),
				nvrhi::BindingSetItem::TypedBuffer_SRV(0, m_OutputSharedBuffer, nvrhi::Format::RGBA16_FLOAT),
				nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
			};
			m_CompositeBindingSets[currentSlot] = device->createBindingSet(desc, m_CompositeBindingLayout);
		}

		m_BindingSetsDirty[currentSlot] = false;
	}

	void OIDNIntegration::SettingsChanged(const Settings& settings)
	{
		m_Enabled = (settings.GeneralSettings.Denoiser == Denoiser::OIDN);
	}

	void OIDNIntegration::ResolutionChanged(uint2 resolution)
	{
		if (m_CurrentResolution.x != resolution.x || m_CurrentResolution.y != resolution.y) {
			m_CurrentResolution = resolution;
			m_ResourcesDirty = true;
		}
	}

	void OIDNIntegration::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

#if defined(ENABLE_OIDN)
		if (!m_DeviceInitialized || !m_Device)
			return;

		auto resolution = GetRenderer()->GetResolution();
		ResolutionChanged(resolution);

		if (m_ResourcesDirty)
			CreateResources();

		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_PreparePipeline || !m_CompositePipeline || !m_PrepareBindingSets[currentSlot] || !m_CompositeBindingSets[currentSlot])
			return;

		auto t0 = std::chrono::high_resolution_clock::now();
		// Step 1: Run PrepareOIDNInputs to unpack and sanitize Color, Albedo, Normal into shared GPU buffers
		{
			nvrhi::ComputeState state;
			state.pipeline = m_PreparePipeline;
			state.bindings = { m_PrepareBindingSets[currentSlot] };
			commandList->setComputeState(state);

			auto threadGroupSize = Util::Math::GetDispatchCount(m_CurrentResolution, 8);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}

		auto t1 = std::chrono::high_resolution_clock::now();

		auto* renderer = GetRenderer();
		auto* device = renderer->GetDevice();
		auto* nvrhiVkDevice = static_cast<nvrhi::vulkan::IDevice*>(device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_VK_Device));

		if (m_SemaphoresInitialized && nvrhiVkDevice) {
			const uint64_t syncVal = ++m_TimelineValue;

			// Step 2: Signal timeline semaphore from Vulkan Graphics queue when CommandList 1 finishes on GPU
			{
				std::scoped_lock lock(renderer->GetExecutionMutex());
				nvrhiVkDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_VkSemVulkanToOIDN, syncVal);
				commandList->close();
				device->executeCommandList(commandList, nvrhi::CommandQueue::Graphics);
				// ZERO CPU wait!
			}

			auto t2 = std::chrono::high_resolution_clock::now();

			// Step 3: Queue OIDN GPU operations asynchronously on GPU stream (returns immediately on CPU!)
			m_Device.waitSemaphoreAsync(m_OidnSemVulkanToOIDN, syncVal);
			m_Filter.executeAsync();
			m_Device.signalSemaphoreAsync(m_OidnSemOIDNToVulkan, syncVal);

			auto t3 = std::chrono::high_resolution_clock::now();

			// Step 4: Reopen command list for PostOIDNComposite and configure wait semaphore on GPU
			commandList->open();
			{
				std::scoped_lock lock(renderer->GetExecutionMutex());
				nvrhiVkDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, m_VkSemOIDNToVulkan, syncVal);
			}

			// Re-write volatile constant buffers to the newly opened command list chunk
			auto* scene = Scene::GetSingleton();
			commandList->writeBuffer(scene->GetCameraBuffer(), scene->GetCameraData(), sizeof(CameraData));
			commandList->writeBuffer(scene->GetFeatureBuffer(), scene->GetFeatureData(), sizeof(FeatureData));

			// Step 5: Run PostOIDNComposite to convert linear HDR to display gamma and write into MainTexture
			{
				nvrhi::ComputeState state;
				state.pipeline = m_CompositePipeline;
				state.bindings = { m_CompositeBindingSets[currentSlot] };
				commandList->setComputeState(state);

				auto threadGroupSize = Util::Math::GetDispatchCount(m_CurrentResolution, 8);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			}

			auto t4 = std::chrono::high_resolution_clock::now();

			static uint32_t frameCount = 0;
			if (++frameCount == 1 || frameCount % 60 == 0) {
				float prepMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
				float submitMs = std::chrono::duration<float, std::milli>(t2 - t1).count();
				float oidnQueueMs = std::chrono::duration<float, std::milli>(t3 - t2).count();
				float compMs = std::chrono::duration<float, std::milli>(t4 - t3).count();
				logger::info("OIDN Timings (CPU ms) - Prep: {:.2f}, VulkanSubmit: {:.2f}, OIDNQueueAsync: {:.2f}, Comp: {:.2f} (Total CPU: {:.2f}ms, GPU fenced)",
					prepMs, submitMs, oidnQueueMs, compMs, prepMs + submitMs + oidnQueueMs + compMs);
			}
		} else {
			// Fallback path with CPU wait if GPU semaphores could not be created
			{
				std::scoped_lock lock(renderer->GetExecutionMutex());
				commandList->close();
				device->executeCommandList(commandList, nvrhi::CommandQueue::Graphics);
				device->waitForIdle();
			}

			auto t2 = std::chrono::high_resolution_clock::now();
			m_Filter.execute();
			auto t3 = std::chrono::high_resolution_clock::now();

			commandList->open();
			auto* scene = Scene::GetSingleton();
			commandList->writeBuffer(scene->GetCameraBuffer(), scene->GetCameraData(), sizeof(CameraData));
			commandList->writeBuffer(scene->GetFeatureBuffer(), scene->GetFeatureData(), sizeof(FeatureData));

			{
				nvrhi::ComputeState state;
				state.pipeline = m_CompositePipeline;
				state.bindings = { m_CompositeBindingSets[currentSlot] };
				commandList->setComputeState(state);

				auto threadGroupSize = Util::Math::GetDispatchCount(m_CurrentResolution, 8);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			}

			auto t4 = std::chrono::high_resolution_clock::now();
		}
#endif
	}

	void OIDNIntegration::ReloadShaders()
	{
		CreatePipelines();
		m_BindingSetsDirty.fill(true);
	}
}
