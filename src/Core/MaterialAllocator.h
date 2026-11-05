#pragma once

#include "Core/DirtyRangeTracker.h"

// Fixed-capacity allocator for the material buffer. Materials are placed in the smallest size class
// that fits, and a freed block goes back to its own class.
class MaterialAllocator : public DirtyRangeTracker
{
public:
	using OffsetType = DirtyRangeTracker::OffsetType;
	static constexpr OffsetType INVALID_OFFSET = UINT64_MAX;

	struct Allocation
	{
		OffsetType offset = INVALID_OFFSET;
		size_t size = 0;
	};

	MaterialAllocator(size_t maxMaterialSize, size_t capacityBytes)
		: DirtyRangeTracker(maxMaterialSize, capacityBytes / maxMaterialSize)
	{
		m_Classes[0].size = 48;
		m_Classes[1].size = 72;
		m_Classes[2].size = maxMaterialSize;
	}

	Allocation Allocate(size_t size)
	{
		std::scoped_lock lock(m_Mutex);

		SizeClass* fit = nullptr;
		for (auto& sizeClass : m_Classes) {
			if (sizeClass.size >= size && (!fit || sizeClass.size < fit->size))
				fit = &sizeClass;
		}

		if (fit) {
			if (!fit->freeOffsets.empty())
				return TakeFreeBlock(*fit);

			if (m_NextOffset + fit->size <= m_Size) {
				const OffsetType offset = m_NextOffset;
				m_NextOffset += fit->size;
				fit->allocated++;
				return { offset, fit->size };
			}

			// The unused space ran out, a free block from a larger class still fits.
			for (auto& sizeClass : m_Classes) {
				if (sizeClass.size > fit->size && !sizeClass.freeOffsets.empty())
					return TakeFreeBlock(sizeClass);
			}
		}

		if (!m_CapacityReported) {
			logger::error("MaterialAllocator::Allocate - Capacity exhausted ({} bytes) allocating {} bytes. Allocated blocks: {} x {}, {} x {}, {} x {}",
				m_Size, size,
				m_Classes[0].allocated, m_Classes[0].size,
				m_Classes[1].allocated, m_Classes[1].size,
				m_Classes[2].allocated, m_Classes[2].size);
			m_CapacityReported = true;
		}

		return {};
	}

	void Release(OffsetType offset, size_t size)
	{
		std::scoped_lock lock(m_Mutex);

		for (auto& sizeClass : m_Classes) {
			if (sizeClass.size == size) {
				sizeClass.freeOffsets.push_back(offset);
				sizeClass.allocated--;
				return;
			}
		}

		logger::critical("MaterialAllocator::Release - No size class for {} bytes at offset {}", size, offset);
	}

private:
	struct SizeClass
	{
		size_t size = 0;
		size_t allocated = 0;
		eastl::vector<OffsetType> freeOffsets;
	};

	static Allocation TakeFreeBlock(SizeClass& sizeClass)
	{
		const OffsetType offset = sizeClass.freeOffsets.back();
		sizeClass.freeOffsets.pop_back();
		sizeClass.allocated++;
		return { offset, sizeClass.size };
	}

	SizeClass m_Classes[3];
	OffsetType m_NextOffset = 0;
	bool m_CapacityReported = false;
};
