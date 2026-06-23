#pragma once

#include "Core/DirtyFlags.h"

#include "Framework/DescriptorTableManager.h"

#include "Mesh.hlsli"

class SkinnedMesh;
class DynamicMesh;

class BaseMesh
{
public:
	struct BufferDescriptor {
		nvrhi::BufferHandle m_Buffer;
		DescriptorHandle m_Descriptor;
	};

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

	// Returns the mesh's geometry descs (identity transform with the local-to-owner transform baked in).
	// DirectMesh holds a single desc; SkinnedMesh/DynamicMesh hold one per partition.
	virtual const eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescs() const = 0;

	RE::BSTriShape* GetTriShape() const { return m_BSTriShape; }

	const eastl::string& GetName() const { return m_Name; }

	RE::TESObjectREFR* GetOwner() const { return m_Owner; }

	RE::TESObjectREFR* GetPrevOwner() const { return m_PrevOwner; }

	// Stores the owner pointer for grouping/comparison only (never dereferenced); returns true if it changed.
	bool SetOwner(RE::TESObjectREFR* owner);

	// Bakes the local-to-owner transform into the geometry descs (computed in the traversal while alive);
	// flags the mesh for a refit if it changed.
	void SetLocalToOwner(const float3x4& localToOwner);

	CESEAdapter::REX::EnumSet<DirtyFlags> GetDirtyFlags() const { return m_DirtyFlags; }

	void ClearDirtyFlags() { m_DirtyFlags = DirtyFlags::None; }

	// Writes one MeshData per geometry into 'out' (starting at out[0]); returns the number written.
	uint32_t WriteMeshData(MeshData* out) const;

protected:
	void MarkDirty(DirtyFlags flag) { m_DirtyFlags.set(flag); }

	// Per-geometry index buffer descriptor index (into the Triangles bindless table).
	virtual uint16_t GetIndexID(size_t geometryIndex) const = 0;

	// Vertex buffer descriptor index (into the Vertices bindless table); shared across the mesh's geometries.
	virtual uint16_t GetVertexID() const = 0;

	static eastl::string MakeDebugName(RE::BSTriShape* bsTriShape);

	static bool ValidateCounts(uint16_t numTriangles, uint32_t numVertices);

	static BufferDescriptor CreateIndexBuffer(RE::BSGraphics::TriShape* triShape);

	static BufferDescriptor CreateVertexBuffer(RE::BSGraphics::TriShape* triShape);

	static nvrhi::rt::GeometryDesc MakeGeometryDesc(nvrhi::IBuffer* indexBuffer, uint32_t indexCount, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, uint32_t vertexCount);

	// Mutable access to the derived mesh's geometry descs, used to bake the local-to-owner transform.
	virtual eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescsMutable() = 0;

	eastl::string m_Name;

	RE::BSTriShape* m_BSTriShape = nullptr;

	RE::TESObjectREFR* m_Owner = nullptr;

	RE::TESObjectREFR* m_PrevOwner = nullptr;

	// Cached local-to-owner transform baked into the geometry descs (computed in the traversal).
	float3x4 m_LocalToOwner;

	// Native 64-bit vertex descriptor (RE::BSGraphics::VertexDesc) for MeshData::Flags.
	uint64_t m_VertexDescRaw = 0;

	// Mesh-owned dirty state consumed by the owning BLASCluster (Visibility => rebuild, Transform/Vertex => refit).
	CESEAdapter::REX::EnumSet<DirtyFlags> m_DirtyFlags = DirtyFlags::Visibility;

	CESEAdapter::REX::EnumSet<State> m_State = State::None;

	// Prevents BSTriShape being destroyed mid-usage
	std::mutex m_BSTriShapeMutex;
};
