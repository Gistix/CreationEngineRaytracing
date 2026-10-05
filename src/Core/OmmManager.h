#pragma once

#include <nvrhi/nvrhi.h>
#include "Types/RE/RE.h"
#include <vector>
#include <memory>
#include <mutex>
#include <ankerl/unordered_dense.h>

class BaseMesh;

namespace omm {
	class GpuBakeNvrhi;
}

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
	static constexpr uint32_t kCurrentVersion = 1;

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
		return magic == kMagic && version == 1 && (format == 1 || format == 2);
	}
};

#pragma pack(pop)

struct OmmResource
{
	nvrhi::BufferHandle bitstreamBuffer;
	nvrhi::BufferHandle perOmmDescsBuffer;
	nvrhi::BufferHandle indexBuffer;
	nvrhi::rt::OpacityMicromapHandle opacityMicromap;
	std::vector<nvrhi::rt::OpacityMicromapUsageCount> indexHistogram;
	uint64_t contentHash = 0;
};

struct PendingOmmBakeTask
{
	uint64_t runtimeKey = 0;
	BaseMesh* targetMesh = nullptr;
	uint64_t submitFence = 0;
	nvrhi::EventQueryHandle eventQuery;

	// GPU outputs from GPU bake dispatch
	nvrhi::BufferHandle ommArrayBuffer;
	nvrhi::BufferHandle ommDescBuffer;
	nvrhi::BufferHandle ommIndexBuffer;
	nvrhi::BufferHandle ommDescArrayHistogramBuffer;
	nvrhi::BufferHandle ommIndexHistogramBuffer;
	nvrhi::BufferHandle ommPostDispatchInfoBuffer;

	// CPU-readable readback staging buffers
	nvrhi::BufferHandle descArrayHistogramReadback;
	nvrhi::BufferHandle indexHistogramReadback;
	nvrhi::BufferHandle postDispatchInfoReadback;
};

class OmmManager
{
public:
	OmmManager();
	~OmmManager();

	[[nodiscard]] bool IsSupported() const noexcept { return m_IsSupported; }

	// Retrieves cached, builds offline, or schedules runtime GPU bake for the given shape.
	// 1. Checks for offline baked binary extra data ("CERT::OMM"). If present, uploads and returns immediately.
	// 2. If absent and runtime OMM fallback is enabled:
	//    - Returns cached OmmResource if already baked.
	//    - Otherwise enqueues an asynchronous GPU bake task, returning nullptr for frame 0 (Any-Hit fallback).
	std::shared_ptr<OmmResource> GetOrCreate(RE::BSTriShape* triShape, BaseMesh* mesh, nvrhi::ICommandList* commandList);

	// Backward compatibility overload for offline-only path
	std::shared_ptr<OmmResource> GetOrCreate(RE::BSTriShape* triShape, nvrhi::ICommandList* commandList)
	{
		return GetOrCreate(triShape, nullptr, commandList);
	}

	// Called every frame to inspect finished bake fences, read back exact usage counts, and attach OMM to meshes.
	void ProcessPendingBakes(nvrhi::ICommandList* commandList, uint64_t completedFence);

	// Unregisters a mesh if it is destroyed before its pending bake finishes
	void CancelPendingBake(BaseMesh* mesh);

private:
	bool m_IsSupported = false;
	std::unique_ptr<omm::GpuBakeNvrhi> m_GpuBaker;

	mutable std::mutex m_CacheMutex;
	ankerl::unordered_dense::map<uint64_t, std::shared_ptr<OmmResource>> m_Cache;

	mutable std::mutex m_PendingMutex;
	std::vector<PendingOmmBakeTask> m_PendingTasks;
	ankerl::unordered_dense::set<uint64_t> m_InFlightKeys;

	// Checks if shape has offline extra data
	std::shared_ptr<OmmResource> TryGetOfflineResource(RE::BSTriShape* triShape, nvrhi::ICommandList* commandList);

	// Dispatches runtime GPU bake task
	bool ScheduleRuntimeGpuBake(RE::BSTriShape* triShape, BaseMesh* mesh, nvrhi::ICommandList* commandList, uint64_t runtimeKey, float alphaCutoff);

	// Lazily initializes GPU baker on first command list
	void EnsureGpuBaker(nvrhi::ICommandList* commandList);

	// Computes 64-bit cache key from mesh geometry and material parameters
	static uint64_t ComputeRuntimeKey(RE::BSTriShape* triShape, BaseMesh* mesh, float alphaCutoff);
};
