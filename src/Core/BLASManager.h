#pragma once

#include "nvrhi/nvrhi.h"
#include "Constants.h"
#include "Types/RE/RE.h"
#include "Utils/Math.h"

#include <ankerl/unordered_dense.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <shared_mutex>
#include <mutex>
#include <vector>
#include <cstring>

struct BLASGeometryKey
{
		void* nativeIndexBuffer = nullptr;
		uint64_t indexOffset = 0;
		uint32_t indexCount = 0;

		void* nativeVertexBuffer = nullptr;
		uint64_t vertexOffset = 0;
		uint32_t vertexCount = 0;
		uint32_t vertexStride = 0;

		uint32_t geometryFlags = 0;

		// Relative local transform inside multi-mesh BLAS
		bool hasRelativeTransform = false;
		float3x4 relativeTransform = Constants::kIdentityTransform;

		// OMM forward compatibility (matches OmmResource / contentHash)
		uint64_t ommHash = 0;

		bool operator==(const BLASGeometryKey& other) const noexcept
		{
			if (nativeIndexBuffer != other.nativeIndexBuffer ||
				indexOffset != other.indexOffset ||
				indexCount != other.indexCount ||
				nativeVertexBuffer != other.nativeVertexBuffer ||
				vertexOffset != other.vertexOffset ||
				vertexCount != other.vertexCount ||
				vertexStride != other.vertexStride ||
				geometryFlags != other.geometryFlags ||
				hasRelativeTransform != other.hasRelativeTransform ||
				ommHash != other.ommHash) {
				return false;
			}

			if (hasRelativeTransform) {
				return std::memcmp(&relativeTransform, &other.relativeTransform, sizeof(float3x4)) == 0;
			}

			return true;
		}
	};

	struct BLASKey
	{
		uint32_t baseFormID = 0; // 0 for orphan clusters
		eastl::vector<BLASGeometryKey> geometries;
		uint32_t buildFlags = 0;
		uint64_t hash = 0;

		void ComputeHash() noexcept
		{
			uint64_t h = 14695981039346656037ull;
			auto hashCombine = [&h](uint64_t val) {
				h ^= val;
				h *= 1099511628211ull;
			};

			hashCombine(baseFormID);
			hashCombine(buildFlags);
			hashCombine(static_cast<uint64_t>(geometries.size()));

			for (const auto& g : geometries) {
				hashCombine(reinterpret_cast<uintptr_t>(g.nativeIndexBuffer));
				hashCombine(g.indexOffset);
				hashCombine(g.indexCount);
				hashCombine(reinterpret_cast<uintptr_t>(g.nativeVertexBuffer));
				hashCombine(g.vertexOffset);
				hashCombine(g.vertexCount);
				hashCombine(g.vertexStride);
				hashCombine(g.geometryFlags);
				hashCombine(g.ommHash);
				hashCombine(g.hasRelativeTransform ? 1ull : 0ull);
				if (g.hasRelativeTransform) {
					const auto* words = reinterpret_cast<const uint64_t*>(&g.relativeTransform);
					for (size_t i = 0; i < sizeof(float3x4) / sizeof(uint64_t); ++i) {
						hashCombine(words[i]);
					}
				}
			}
			hash = h;
		}

		bool operator==(const BLASKey& other) const noexcept
		{
			if (hash != other.hash ||
				baseFormID != other.baseFormID ||
				buildFlags != other.buildFlags ||
				geometries.size() != other.geometries.size()) {
				return false;
			}

			return geometries == other.geometries;
		}
	};

	struct BLASKeyHash
	{
		using is_avalanching = void;
		size_t operator()(const BLASKey& key) const noexcept
		{
			return static_cast<size_t>(key.hash);
		}
	};

	class BLASResource
	{
	public:
		nvrhi::rt::AccelStructHandle m_BLAS;
		BLASKey m_Key;
		std::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;
		nvrhi::rt::AccelStructBuildFlags m_BuildFlags = nvrhi::rt::AccelStructBuildFlags::None;
		std::vector<nvrhi::BufferHandle> m_BufferRefs;

		eastl::string m_DebugName;
		uint64_t m_LastBuildFrame = Constants::INVALID_FRAME_INDEX;
		uint64_t m_LastUsedFrame = Constants::INVALID_FRAME_INDEX;
		uint32_t m_UpdateCount = 0;
		bool m_IsShared = true;
		bool m_IsDirty = true;
		bool m_IsUpdatable = false;

		[[nodiscard]] bool IsValid() const noexcept { return m_BLAS != nullptr; }
	};

	void* GetNativeBufferPointer(nvrhi::IBuffer* buffer) noexcept;

	class BLASManager
	{
	public:
		explicit BLASManager(nvrhi::IDevice* device = nullptr);
		~BLASManager() = default;

		// Look up or register a shared BLAS resource for static single/multi-mesh or orphan geometry
		eastl::shared_ptr<BLASResource> GetOrCreateSharedBLAS(
			const BLASKey& key,
			const std::vector<nvrhi::rt::GeometryDesc>& descs,
			nvrhi::rt::AccelStructBuildFlags buildFlags,
			const std::vector<nvrhi::BufferHandle>& bufferRefs,
			const eastl::string& debugName);

		// Create a dedicated (unshared) BLAS for dynamic or skinned clusters
		eastl::shared_ptr<BLASResource> CreateDedicatedBLAS(
			const std::vector<nvrhi::rt::GeometryDesc>& descs,
			nvrhi::rt::AccelStructBuildFlags buildFlags,
			const std::vector<nvrhi::BufferHandle>& bufferRefs,
			const eastl::string& debugName);

		// Staging of relative transforms for multi-mesh BLAS geometries during Phase G
		uint64_t StageRelativeTransform(const float3x4& transform);

		// Flushes any staged relative transforms to GPU memory before BLAS building
		void FlushTransforms(nvrhi::ICommandList* commandList);

		// Builds or refits the specified BLAS resource if dirty or needed
		void BuildResource(
			nvrhi::ICommandList* commandList,
			const eastl::shared_ptr<BLASResource>& resource,
			bool forceRebuild = false);

		// Periodic maintenance: releases unreferenced BLASes after GPU fence completion
		void ProcessPendingReleases(uint64_t completedFence, uint64_t currentFrame);

		[[nodiscard]] nvrhi::IBuffer* GetSharedTransformBuffer() const noexcept { return m_SharedTransformBuffer; }
		[[nodiscard]] size_t GetCachedBLASCount() const;
		[[nodiscard]] size_t GetDedicatedBLASCount() const;

	private:
		nvrhi::IDevice* m_Device = nullptr;

		mutable std::shared_mutex m_CacheMutex;
		ankerl::unordered_dense::map<BLASKey, eastl::shared_ptr<BLASResource>, BLASKeyHash> m_SharedCache;

		// Shared static transform buffer for multi-mesh relative transforms
		static constexpr size_t kMaxSharedTransforms = 4096;
		nvrhi::BufferHandle m_SharedTransformBuffer;
		std::mutex m_TransformMutex;
		std::vector<float3x4> m_StagedTransforms;
		uint32_t m_NextTransformSlot = 0;
		uint32_t m_UploadedTransformCount = 0;

		struct PendingRelease
		{
			nvrhi::rt::AccelStructHandle blas;
			uint64_t fence = UINT64_MAX;
		};
		std::mutex m_ReleaseMutex;
		std::vector<PendingRelease> m_PendingReleases;
	};
