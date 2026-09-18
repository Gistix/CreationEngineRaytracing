#pragma once

#include "nvrhi/nvrhi.h"
#include "Types/RE/RE.h"
#include <vector>
#include <memory>
#include <mutex>
#include <EASTL/hash_map.h>

namespace CERT
{
#pragma pack(push, 1)

	// 8 bytes - matches D3D12_RAYTRACING_OPACITY_MICROMAP_DESC and VkMicromapTriangleEXT
	struct OmmDescEntry
	{
		uint32_t byteOffset = 0;
		uint16_t subdivisionLevel = 0;
		uint16_t format = 0; // 1 = OC1_2_State, 2 = OC1_4_State
	};

	// 12 bytes - matches nvrhi::rt::OpacityMicromapUsageCount, D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY and VkMicromapUsageEXT
	struct OmmUsageEntry
	{
		uint32_t count = 0;
		uint32_t subdivisionLevel = 0;
		uint32_t format = 0; // 1 = OC1_2_State, 2 = OC1_4_State
	};

	struct OmmPayloadHeader
	{
		static constexpr uint32_t kMagic = 0x314D4D4F; // 'OMM1'
		static constexpr uint32_t kCurrentVersion = 2;

		uint32_t magic = kMagic;
		uint32_t version = kCurrentVersion;

		uint64_t contentHash = 0;          // 64-bit hash for fast runtime GPU deduplication

		uint16_t format = 1;              // 1 = OC1_2_State (1 bit/micro-tri), 2 = OC1_4_State (2 bits/micro-tri)
		uint16_t maxSubdivisionLevel = 6;

		uint32_t numTriangles = 0;
		uint32_t indexByteSize = 0;        // numTriangles * sizeof(uint16_t)

		uint32_t descArrayCount = 0;       // number of OmmDescEntry elements
		uint32_t descArrayByteSize = 0;    // descArrayCount * sizeof(OmmDescEntry)

		uint32_t descHistogramCount = 0;   // number of OmmUsageEntry elements for OMM array build
		uint32_t descHistogramByteSize = 0;

		uint32_t indexHistogramCount = 0;  // number of OmmUsageEntry elements for BLAS geometry attachment
		uint32_t indexHistogramByteSize = 0;

		uint32_t ommArrayByteSize = 0;     // Raw OMM bitstream size in bytes
		uint32_t reserved = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return magic == kMagic && (version == 1 || version == 2) && (format == 1 || format == 2);
		}
	};

	struct OmmPayloadHeaderV1
	{
		uint32_t magic;
		uint32_t version;
		uint16_t format;
		uint16_t maxSubdivisionLevel;
		uint32_t numTriangles;
		uint32_t indexByteSize;
		uint32_t ommArrayByteSize;
		uint32_t reserved;
	};

#pragma pack(pop)

	struct OmmResource
	{
		nvrhi::BufferHandle bitstreamBuffer;
		nvrhi::BufferHandle perOmmDescsBuffer;
		nvrhi::BufferHandle indexBuffer;
		nvrhi::rt::OpacityMicromapHandle opacityMicromap;
		std::vector<nvrhi::rt::OpacityMicromapUsageCount> indexHistogram;
	};

	class OmmManager
	{
	public:
		OmmManager();
		~OmmManager() = default;

		[[nodiscard]] bool IsSupported() const { return m_IsSupported; }

		// Retrieves cached or builds new GPU OMM resources for the given shape.
		// Multiple BSTriShape instances sharing the same baked OMM data reuse the same GPU buffers and OMM array.
		std::shared_ptr<OmmResource> GetOrCreate(RE::BSTriShape* triShape, nvrhi::ICommandList* commandList);

	private:
		bool m_IsSupported = false;
		mutable std::mutex m_CacheMutex;
		eastl::hash_map<uint64_t, std::shared_ptr<OmmResource>> m_Cache;
	};

} // namespace CERT
