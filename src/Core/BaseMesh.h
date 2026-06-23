#pragma once

#include "Core/DirtyFlags.h"

class SkinnedMesh;
class DynamicMesh;

class BaseMesh
{
public:
	enum class State : uint8_t
	{
		None = 0,
		Hidden = 1 << 0,
		Detached = 1 << 1,
		DismemberHidden = 1 << 2,
		SubIndexHidden = 1 << 3,
		Destroyed = 1 << 4
	};

	virtual ~BaseMesh() = default;

	// Constructs the appropriate mesh type (DirectMesh, SkinnedMesh or DynamicMesh) for the given geometry.
	static eastl::shared_ptr<BaseMesh> Create(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	// Returns true if the hidden state changed (which flags the mesh structurally dirty).
	bool SetHidden(bool hidden);

	bool IsHidden() const;

	void OnDestroy();

	virtual SkinnedMesh* AsSkinnedMesh() { return nullptr; }

	virtual DynamicMesh* AsDynamicMesh() { return nullptr; }

	// CPU-side per-frame update: detect whether the mesh's geometry data changed (lazy).
	// Returns true if changed (so the owning cluster is flagged for refit). No-op for static meshes.
	virtual bool UpdateData() { return false; }

	// GPU-side per-frame upload of any pending data (flag-gated); runs in the TLAS pass. No-op otherwise.
	virtual void UploadBuffers([[maybe_unused]] nvrhi::ICommandList* commandList) {}

	// True for meshes whose vertex data changes per frame, so their cluster BLAS must be refit.
	virtual bool IsUpdatable() const { return false; }

	const eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescs() const { return m_GeometryDescs; }

	RE::BSTriShape* GetTriShape() const { return m_BSTriShape; }

	RE::TESObjectREFR* GetOwner() const { return m_Owner; }

	RE::TESObjectREFR* GetPrevOwner() const { return m_PrevOwner; }

	// Stores the owner pointer for grouping/comparison only (never dereferenced); returns true if it changed.
	bool SetOwner(RE::TESObjectREFR* owner);

	// Bakes the local-to-owner transform into the geometry descs (computed in the traversal while alive);
	// flags the mesh for a refit if it changed.
	void SetLocalToOwner(const float3x4& localToOwner);

	CESEAdapter::REX::EnumSet<DirtyFlags> GetDirtyFlags() const { return m_DirtyFlags; }

	void ClearDirtyFlags() { m_DirtyFlags = DirtyFlags::None; }

protected:
	void MarkDirty(DirtyFlags flag) { m_DirtyFlags.set(flag); }

	static eastl::string MakeDebugName(RE::BSTriShape* bsTriShape);

	static bool ValidateCounts(uint16_t numTriangles, uint32_t numVertices, RE::BSGraphics::TriShape* triShape);

	static nvrhi::BufferHandle CreateIndexBuffer(RE::BSGraphics::TriShape* triShape);

	static nvrhi::BufferHandle CreateVertexBuffer(RE::BSGraphics::TriShape* triShape);

	static nvrhi::rt::GeometryDesc MakeGeometryDesc(nvrhi::IBuffer* indexBuffer, uint32_t indexCount, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, uint32_t vertexCount);

	eastl::string m_Name;

	// Geometry descs (identity transform) provided to the owning BLASCluster, which bakes the local-to-owner transform.
	eastl::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;

	RE::BSTriShape* m_BSTriShape = nullptr;

	RE::TESObjectREFR* m_Owner = nullptr;

	RE::TESObjectREFR* m_PrevOwner = nullptr;

	// Cached local-to-owner transform baked into m_GeometryDescs (computed in the traversal).
	float3x4 m_LocalToOwner;

	// Mesh-owned dirty state consumed by the owning BLASCluster (Visibility => rebuild, Transform/Vertex => refit).
	CESEAdapter::REX::EnumSet<DirtyFlags> m_DirtyFlags = DirtyFlags::Visibility;

	CESEAdapter::REX::EnumSet<State> m_State = State::None;

	// Prevents BSTriShape being destroyed mid-usage
	std::mutex m_BSTriShapeMutex;
};
