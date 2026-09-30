#include "Core/BLASManager.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"

#include <nvrhi/utils.h>

void* GetNativeBufferPointer(nvrhi::IBuffer* buffer) noexcept
{
		if (!buffer)
			return nullptr;

		auto* renderer = Renderer::GetSingleton();
		if (renderer && renderer->IsVulkan()) {
			void* p = buffer->getNativeObject(nvrhi::ObjectTypes::VK_Buffer);
			return p ? p : buffer;
		}

		void* p = buffer->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
		return p ? p : buffer;
	}

	BLASManager::BLASManager(nvrhi::IDevice* device)
		: m_Device(device)
	{
		m_StagedTransforms.reserve(256);

		auto desc = nvrhi::BufferDesc()
			.setByteSize(kMaxSharedTransforms * sizeof(float3x4))
			.setCanHaveRawViews(true)
			.setIsAccelStructBuildInput(true)
			.setInitialState(nvrhi::ResourceStates::AccelStructBuildInput)
			.setKeepInitialState(true)
			.setDebugName("BLASManager SharedTransforms");

		m_SharedTransformBuffer = m_Device->createBuffer(desc);
	}

	eastl::shared_ptr<BLASResource> BLASManager::GetOrCreateSharedBLAS(
		const BLASKey& key,
		const std::vector<nvrhi::rt::GeometryDesc>& descs,
		nvrhi::rt::AccelStructBuildFlags buildFlags,
		const std::vector<nvrhi::BufferHandle>& bufferRefs,
		const eastl::string& debugName)
	{
		const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

		{
			std::shared_lock lock(m_CacheMutex);
			auto it = m_SharedCache.find(key);
			if (it != m_SharedCache.end()) {
				it->second->m_LastUsedFrame = frameIndex;
				return it->second;
			}
		}

		std::unique_lock lock(m_CacheMutex);
		auto it = m_SharedCache.find(key);
		if (it != m_SharedCache.end()) {
			it->second->m_LastUsedFrame = frameIndex;
			return it->second;
		}

		auto resource = eastl::make_shared<BLASResource>();
		resource->m_Key = key;
		resource->m_GeometryDescs = descs;
		resource->m_BuildFlags = buildFlags;
		resource->m_BufferRefs = bufferRefs;
		resource->m_DebugName = debugName;
		resource->m_LastUsedFrame = frameIndex;
		resource->m_IsShared = true;
		resource->m_IsDirty = true;
		resource->m_IsUpdatable = false;

		m_SharedCache[key] = resource;
		return resource;
	}

	eastl::shared_ptr<BLASResource> BLASManager::CreateDedicatedBLAS(
		const std::vector<nvrhi::rt::GeometryDesc>& descs,
		nvrhi::rt::AccelStructBuildFlags buildFlags,
		const std::vector<nvrhi::BufferHandle>& bufferRefs,
		const eastl::string& debugName)
	{
		const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

		auto resource = eastl::make_shared<BLASResource>();
		resource->m_GeometryDescs = descs;
		resource->m_BuildFlags = buildFlags;
		resource->m_BufferRefs = bufferRefs;
		resource->m_DebugName = debugName;
		resource->m_LastUsedFrame = frameIndex;
		resource->m_IsShared = false;
		resource->m_IsDirty = true;
		resource->m_IsUpdatable = true;

		return resource;
	}

	uint64_t BLASManager::StageRelativeTransform(const float3x4& transform)
	{
		std::scoped_lock lock(m_TransformMutex);

		// Check if identical transform is already staged
		for (size_t i = 0; i < m_StagedTransforms.size(); ++i) {
			if (std::memcmp(&m_StagedTransforms[i], &transform, sizeof(float3x4)) == 0) {
				return static_cast<uint64_t>(i * sizeof(float3x4));
			}
		}

		if (m_NextTransformSlot >= kMaxSharedTransforms) {
			logger::error("BLASManager: Shared transform buffer capacity exceeded ({})", kMaxSharedTransforms);
			return 0;
		}

		const uint32_t slot = m_NextTransformSlot++;
		m_StagedTransforms.push_back(transform);
		return static_cast<uint64_t>(slot * sizeof(float3x4));
	}

	void BLASManager::FlushTransforms(nvrhi::ICommandList* commandList)
	{
		std::scoped_lock lock(m_TransformMutex);

		if (m_NextTransformSlot > m_UploadedTransformCount && m_SharedTransformBuffer) {
			const size_t newCount = m_NextTransformSlot - m_UploadedTransformCount;
			const size_t byteOffset = m_UploadedTransformCount * sizeof(float3x4);
			const size_t byteSize = newCount * sizeof(float3x4);

			commandList->writeBuffer(m_SharedTransformBuffer, &m_StagedTransforms[m_UploadedTransformCount], byteSize, byteOffset);
			m_UploadedTransformCount = m_NextTransformSlot;
		}
	}

	void BLASManager::BuildResource(
		nvrhi::ICommandList* commandList,
		const eastl::shared_ptr<BLASResource>& resource,
		bool forceRebuild)
	{
		if (!resource || resource->m_GeometryDescs.empty())
			return;

		auto* renderer = Renderer::GetSingleton();
		const auto frameIndex = renderer->GetFrameIndex();

		// Deduplication: if already built on this frame, skip
		if (resource->m_LastBuildFrame == frameIndex)
			return;

		// If resource is already valid and static (not updatable) and not dirty, skip
		if (resource->IsValid() && !resource->m_IsDirty && !resource->m_IsUpdatable) {
			resource->m_LastBuildFrame = frameIndex;
			return;
		}

		FlushTransforms(commandList);

		auto blasDesc = nvrhi::rt::AccelStructDesc()
			.setIsTopLevel(false)
			.setDebugName(resource->m_DebugName.c_str());

		blasDesc.buildFlags = resource->m_BuildFlags;
		blasDesc.bottomLevelGeometries = resource->m_GeometryDescs;

		const bool allocate = !resource->m_BLAS;
		bool needsAllocation = allocate;

		if (!needsAllocation && (forceRebuild || !resource->m_IsUpdatable)) {
			auto prebuildInfo = m_Device->getAccelStructPreBuildInfo(blasDesc);
			needsAllocation = prebuildInfo.resultMaxSizeInBytes > resource->m_BLAS->getBufferSize();
		}

		if (needsAllocation) {
			resource->m_BLAS = m_Device->createAccelStruct(blasDesc);
		}

		if (resource->m_IsUpdatable && resource->IsValid() && !needsAllocation && !forceRebuild) {
			blasDesc.buildFlags |= nvrhi::rt::AccelStructBuildFlags::PerformUpdate;
		}

		nvrhi::utils::BuildBottomLevelAccelStruct(commandList, resource->m_BLAS, blasDesc);

		resource->m_LastBuildFrame = frameIndex;
		resource->m_IsDirty = false;
	}

	void BLASManager::ProcessPendingReleases(uint64_t completedFence, uint64_t currentFrame)
	{
		// 1. Evict unreferenced shared BLASes older than 1200 frames (~20s at 60fps)
		constexpr uint64_t kMaxUnusedFrames = 1200;

		{
			std::unique_lock lock(m_CacheMutex);
			for (auto it = m_SharedCache.begin(); it != m_SharedCache.end(); ) {
				// use_count == 1 means only m_SharedCache holds a reference
				if (it->second.use_count() == 1 &&
					it->second->m_LastUsedFrame != Constants::INVALID_FRAME_INDEX &&
					(currentFrame - it->second->m_LastUsedFrame) > kMaxUnusedFrames) {
					
					if (it->second->m_BLAS) {
						std::scoped_lock rLock(m_ReleaseMutex);
						m_PendingReleases.push_back({ eastl::move(it->second->m_BLAS), Renderer::GetSingleton()->GetFrameIndex() });
					}
					it = m_SharedCache.erase(it);
				} else {
					++it;
				}
			}
		}

		// 2. Release GPU acceleration structures whose fences have passed
		{
			std::scoped_lock lock(m_ReleaseMutex);
			auto it = std::remove_if(m_PendingReleases.begin(), m_PendingReleases.end(),
				[completedFence](const PendingRelease& p) {
					return p.fence <= completedFence;
				});
			m_PendingReleases.erase(it, m_PendingReleases.end());
		}
	}

	size_t BLASManager::GetCachedBLASCount() const
	{
		std::shared_lock lock(m_CacheMutex);
		return m_SharedCache.size();
	}

	size_t BLASManager::GetDedicatedBLASCount() const
	{
		return 0;
	}
