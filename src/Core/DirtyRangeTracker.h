#pragma once

#include <eastl/sort.h>
#include <eastl/vector.h>
#include <mutex>

class DirtyRangeTracker
{
public:
	using OffsetType = uint64_t;
	using SizeType = uint64_t;

	DirtyRangeTracker(size_t slotSize, size_t slotCount)
		: m_SlotSize(slotSize)
		, m_Size(slotSize * slotCount)
	{
		m_Data.resize(m_Size);
	}

	void Write(OffsetType offset, const void* data, size_t size)
	{
		std::scoped_lock lock(m_Mutex);

		if (size > m_SlotSize) {
			logger::critical("DirtyRangeTracker::Write - Data size ({}) exceeds slot size ({})", size, m_SlotSize);
			return;
		}

		if (offset + size > m_Size) {
			logger::critical("DirtyRangeTracker::Write - Data write out of bounds (offset={}, size={}, capacity={})",
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

	SizeType GetCapacity() const { return m_Size; }

	void MarkAllDirty()
	{
		std::scoped_lock lock(m_Mutex);
		m_DirtyRanges.push_back({ 0, m_Size });
	}

	eastl::vector<eastl::pair<OffsetType, OffsetType>> ConsumeDirtyRanges()
	{
		std::scoped_lock lock(m_Mutex);

		if (m_DirtyRanges.empty())
			return {};

		eastl::vector<eastl::pair<OffsetType, OffsetType>> ranges;
		ranges.swap(m_DirtyRanges);

		// Sort by offset, then merge adjacent/overlapping ranges
		eastl::sort(ranges.begin(), ranges.end(),
			[](const auto& a, const auto& b) { return a.first < b.first; });

		size_t dst = 0;
		for (size_t src = 1; src < ranges.size(); ++src) {
			auto& last = ranges[dst];
			const auto& cur = ranges[src];
			OffsetType lastEnd = last.first + last.second;
			if (cur.first <= lastEnd) {
				OffsetType newEnd = cur.first + cur.second > lastEnd ? cur.first + cur.second : lastEnd;
				last.second = newEnd - last.first;
			} else {
				++dst;
				if (dst != src)
					ranges[dst] = cur;
			}
		}
		ranges.resize(dst + 1);

		return ranges;
	}

protected:
	size_t m_SlotSize;
	size_t m_Size;
	eastl::vector<uint8_t> m_Data;
	eastl::vector<eastl::pair<OffsetType, OffsetType>> m_DirtyRanges;
	mutable std::mutex m_Mutex;
};
