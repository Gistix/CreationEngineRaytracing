#pragma once

#include "Core/DirtyRangeTracker.h"

class ResourceSlotManager : public DirtyRangeTracker
{
public:
	using OffsetType = DirtyRangeTracker::OffsetType;
	using SizeType = DirtyRangeTracker::SizeType;
	static constexpr OffsetType INVALID_OFFSET = UINT64_MAX;

	ResourceSlotManager(size_t slotSize, size_t initialSlotCount, size_t growStepSlots)
		: DirtyRangeTracker(slotSize, initialSlotCount)
		, m_GrowStepSlots(growStepSlots)
	{
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
			if (m_GrowStepSlots == 0) {
				if (!m_CapacityReported) {
					logger::error("ResourceSlotManager::Allocate - Fixed capacity exhausted ({} slots, {} bytes per slot)", m_Size / m_SlotSize, m_SlotSize);
					m_CapacityReported = true;
				}
				return INVALID_OFFSET;
			}
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

	bool ConsumeGrowFlag()
	{
		std::scoped_lock lock(m_Mutex);
		bool grew = m_DidGrow;
		m_DidGrow = false;
		return grew;
	}

	SizeType GetUsedByteSize() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_NextOffset;
	}

private:
	size_t m_GrowStepSlots;
	SizeType m_NextOffset = 0;
	eastl::vector<OffsetType> m_FreeOffsets;
	bool m_DidGrow = false;
	bool m_CapacityReported = false;
};
