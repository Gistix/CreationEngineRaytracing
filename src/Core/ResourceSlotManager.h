#pragma once

#include <eastl/vector.h>
#include <mutex>

class ResourceSlotManager
{
public:
	using OffsetType = uint64_t;
	using SizeType = uint64_t;

	ResourceSlotManager(size_t slotSize, size_t initialSlotCount, size_t growStepSlots)
		: m_SlotSize(slotSize)
		, m_GrowStepSlots(growStepSlots)
		, m_Size(slotSize * initialSlotCount)
	{
		m_Data.resize(m_Size);
	}

	OffsetType Allocate()
	{
		std::scoped_lock lock(m_Mutex);

		if (!m_FreeOffsets.empty()) {
			OffsetType offset = m_FreeOffsets.back();
			m_FreeOffsets.pop_back();
			return offset;
		}

		if (m_NextOffset + m_SlotSize > m_Size) {
			m_Size += m_SlotSize * m_GrowStepSlots;
			m_Data.resize(m_Size);
			m_DidGrow = true;
		}

		OffsetType offset = m_NextOffset;
		m_NextOffset += m_SlotSize;
		return offset;
	}

	void Release(OffsetType offset)
	{
		std::scoped_lock lock(m_Mutex);
		m_FreeOffsets.push_back(offset);
	}

	void Write(OffsetType offset, const void* data, size_t size)
	{
		std::scoped_lock lock(m_Mutex);

		if (size > m_SlotSize) {
			logger::critical("ResourceSlotManager::Write - Data size ({}) exceeds slot size ({})", size, m_SlotSize);
			return;
		}

		if (offset + size > m_Size) {
			logger::critical("ResourceSlotManager::Write - Data write out of bounds (offset={}, size={}, capacity={})",
				offset, size, m_Size);
			return;
		}

		if (std::memcmp(m_Data.data() + offset, data, size) == 0)
			return;

		std::memcpy(m_Data.data() + offset, data, size);
		m_DirtyRanges.push_back({ offset, size });
	}

	void* GetMirror() { return m_Data.data(); }

	SizeType GetSlotSize() const { return m_SlotSize; }

	uint32_t GetIndexFromOffset(OffsetType offset) const
	{
		return static_cast<uint32_t>(offset / m_SlotSize);
	}

	// Thread-safe: atomically retrieves capacity and marks whether growth is needed.
	SizeType GetCapacity() const { return m_Size; }

	// Atomically reads the grow flag (and optionally clears it). Returns true if
	// the CPU mirror grew since the last consume.
	bool ConsumeGrowFlag()
	{
		std::scoped_lock lock(m_Mutex);
		bool grew = m_DidGrow;
		m_DidGrow = false;
		return grew;
	}

	// Atomically reads the used byte size (the highest allocated offset).
	SizeType GetUsedByteSize() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_NextOffset;
	}

	// Marks the range [0, GetUsedByteSize()) as dirty for bulk restaging after a buffer resize.
	// Bypasses the per-slot size check since this is used internally to restage the entire mirror.
	void MarkAllDirty()
	{
		std::scoped_lock lock(m_Mutex);
		m_DirtyRanges.push_back({ 0, m_NextOffset });
	}

	// Atomically swaps out the dirty ranges for an empty vector. The returned ranges
	// are safe to iterate and upload without holding the mutex.
	eastl::vector<eastl::pair<OffsetType, OffsetType>> ConsumeDirtyRanges()
	{
		std::scoped_lock lock(m_Mutex);
		eastl::vector<eastl::pair<OffsetType, OffsetType>> ranges;
		ranges.swap(m_DirtyRanges);
		return ranges;
	}

private:
	size_t m_SlotSize;
	size_t m_GrowStepSlots;
	size_t m_Size = 0;
	SizeType m_NextOffset = 0;

	eastl::vector<uint8_t> m_Data;
	eastl::vector<OffsetType> m_FreeOffsets;
	eastl::vector<eastl::pair<OffsetType, OffsetType>> m_DirtyRanges;
	bool m_DidGrow = false;
	mutable std::mutex m_Mutex;
};
