#include "Core/BLASManager.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"

#include <nvrhi/utils.h>

#include <algorithm>
#include <cassert>

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
		m_TransformRefCounts.reserve(256);

		m_TransformBufferCapacity = kInitialSharedTransforms;
		m_SharedTransformBuffer = CreateTransformBuffer(m_TransformBufferCapacity);
	}

	nvrhi::BufferHandle BLASManager::CreateTransformBuffer(uint32_t capacity) const
	{
		auto desc = nvrhi::BufferDesc()
			.setByteSize(static_cast<uint64_t>(capacity) * sizeof(float3x4))
			.setCanHaveRawViews(true)
			.setIsAccelStructBuildInput(true)
			.setInitialState(nvrhi::ResourceStates::AccelStructBuildInput)
			.setKeepInitialState(true)
			.setDebugName("BLASManager SharedTransforms");

		return m_Device->createBuffer(desc);
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

		// Give every transformed geometry a slot in the shared transform buffer. Only the offset is
		// stored here; BuildResource binds the buffer itself, since it can be replaced when it grows.
		if (key.geometries.size() != resource->m_GeometryDescs.size()) {
			// descs[i] must pair with key.geometries[i]. Without that pairing no slot can be assigned
			// safely, so build untransformed rather than bind another geometry's transform.
			logger::error("BLASManager: '{}' has {} geometry descs but {} key geometries; relative transforms dropped",
				debugName.c_str(), resource->m_GeometryDescs.size(), key.geometries.size());

			for (auto& geometryDesc : resource->m_GeometryDescs) {
				geometryDesc.transformBuffer = nullptr;
				geometryDesc.transformBufferOffset = 0;
				geometryDesc.useTransform = false;
			}
		} else {
			const size_t geometryCount = key.geometries.size();

			std::scoped_lock transformLock(m_TransformMutex);
			for (size_t i = 0; i < geometryCount; ++i) {
				const auto& geometryKey = key.geometries[i];
				if (!geometryKey.hasRelativeTransform)
					continue;

				const uint32_t slot = AcquireTransformSlot(geometryKey.relativeTransform);
				resource->m_TransformSlots.push_back(slot);

				auto& geometryDesc = resource->m_GeometryDescs[i];
				geometryDesc.transformBuffer = nullptr;
				geometryDesc.transformBufferOffset = static_cast<uint64_t>(slot) * sizeof(float3x4);
				geometryDesc.useTransform = true;
			}
		}

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

	uint32_t BLASManager::AcquireTransformSlot(const float3x4& transform)
	{
		// Identical transforms share one slot.
		auto [it, inserted] = m_TransformSlotLookup.try_emplace(TransformKey{ transform }, 0u);
		if (!inserted) {
			++m_TransformRefCounts[it->second];
			return it->second;
		}

		uint32_t slot;
		if (!m_FreeTransformSlots.empty()) {
			slot = m_FreeTransformSlots.back();
			m_FreeTransformSlots.pop_back();
			m_StagedTransforms[slot] = transform;
		} else {
			// Grows the CPU mirror only; FlushTransforms resizes the GPU buffer on the render thread.
			slot = static_cast<uint32_t>(m_StagedTransforms.size());
			m_StagedTransforms.push_back(transform);
			m_TransformRefCounts.push_back(0);
		}

		it->second = slot;
		m_TransformRefCounts[slot] = 1;

		m_DirtyTransformBegin = std::min(m_DirtyTransformBegin, slot);
		m_DirtyTransformEnd = std::max(m_DirtyTransformEnd, slot + 1);

		return slot;
	}

	void BLASManager::ReleaseTransformSlots(const std::vector<uint32_t>& slots)
	{
		for (const uint32_t slot : slots) {
			assert(slot < m_TransformRefCounts.size() && m_TransformRefCounts[slot] > 0);
			if (--m_TransformRefCounts[slot] == 0) {
				m_TransformSlotLookup.erase(TransformKey{ m_StagedTransforms[slot] });
				m_FreeTransformSlots.push_back(slot);
			}
		}
	}

	void BLASManager::FlushTransforms(nvrhi::ICommandList* commandList)
	{
		std::scoped_lock lock(m_TransformMutex);

		if (m_DirtyTransformBegin >= m_DirtyTransformEnd)
			return;

		// Out of GPU slots: replace the buffer with a larger one and re-upload every slot. Builds
		// already recorded keep the old buffer alive through their command list.
		const uint32_t slotCount = static_cast<uint32_t>(m_StagedTransforms.size());
		if (slotCount > m_TransformBufferCapacity) {
			uint32_t capacity = m_TransformBufferCapacity;
			while (capacity < slotCount)
				capacity *= 2;

			logger::info("BLASManager: Growing shared transform buffer from {} to {} slots", m_TransformBufferCapacity, capacity);

			m_SharedTransformBuffer = CreateTransformBuffer(capacity);
			m_TransformBufferCapacity = capacity;

			m_DirtyTransformBegin = 0;
			m_DirtyTransformEnd = slotCount;
		}

		const size_t byteOffset = static_cast<size_t>(m_DirtyTransformBegin) * sizeof(float3x4);
		const size_t byteSize = static_cast<size_t>(m_DirtyTransformEnd - m_DirtyTransformBegin) * sizeof(float3x4);

		commandList->writeBuffer(m_SharedTransformBuffer, &m_StagedTransforms[m_DirtyTransformBegin], byteSize, byteOffset);

		m_DirtyTransformBegin = UINT32_MAX;
		m_DirtyTransformEnd = 0;
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

		// Shared resources store only their slot offset; bind the current buffer (FlushTransforms
		// above may have just replaced it).
		if (resource->m_IsShared) {
			for (auto& geometryDesc : blasDesc.bottomLevelGeometries) {
				if (geometryDesc.useTransform)
					geometryDesc.transformBuffer = m_SharedTransformBuffer;
			}
		}

		const bool allocate = !resource->m_BLAS;
		bool needsAllocation = allocate;

		if (!needsAllocation && (forceRebuild || !resource->m_IsUpdatable)) {
			auto prebuildInfo = m_Device->getAccelStructPreBuildInfo(blasDesc);
			needsAllocation = prebuildInfo.resultMaxSizeInBytes > resource->GetAllocatedMemory();
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
						m_PendingReleases.push_back({ eastl::move(it->second->m_BLAS), Renderer::GetSingleton()->GetLastSubmittedFence() });
					}

					// Only the cache references this resource (use_count == 1), so no cluster - and
					// therefore no TLAS instance, build or hit - can still resolve to its slots.
					if (!it->second->m_TransformSlots.empty()) {
						std::scoped_lock transformLock(m_TransformMutex);
						ReleaseTransformSlots(it->second->m_TransformSlots);
						it->second->m_TransformSlots.clear();
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

	void BLASManager::GetBLASMemoryStats(uint32_t& outSharedCount, uint64_t& outSharedBytes, uint64_t& outTransformBytes) const
	{
		std::shared_lock lock(m_CacheMutex);
		outSharedCount = static_cast<uint32_t>(m_SharedCache.size());
		outSharedBytes = 0;
		for (const auto& [key, res] : m_SharedCache) {
			if (res && res->m_BLAS) {
				outSharedBytes += res->GetAllocatedMemory();
			}
		}

		// The buffer handle is replaced on the render thread when it grows; read the capacity under
		// its mutex instead of touching the handle.
		std::scoped_lock transformLock(m_TransformMutex);
		outTransformBytes = static_cast<uint64_t>(m_TransformBufferCapacity) * sizeof(float3x4);
	}

	void BLASManager::GetPendingBLASMemoryStats(uint32_t& outPendingCount, uint64_t& outPendingBytes) const
	{
		std::scoped_lock lock(m_ReleaseMutex);
		outPendingCount = static_cast<uint32_t>(m_PendingReleases.size());
		outPendingBytes = 0;
		for (const auto& p : m_PendingReleases) {
			if (p.blas) {
				outPendingBytes += p.blas->getBufferSize();
			}
		}
	}
