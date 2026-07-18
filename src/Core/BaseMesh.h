#pragma once

#include "Constants.h"
#include "Core/DirtyFlags.h"
#include "Core/Material/MaterialBase.h"
#include "Core/Skyrim/Properties.h"
#include "Framework/DescriptorTableManager.h"
#include "Interop/LandLODUpdate.hlsli"
#include "Mesh.hlsli"
#include "Util.h"

class SkinnedMesh;
class DynamicMesh;
class BLASCluster;

class BaseMesh
{
	void ClearDirtyFlags() { m_DirtyFlags.reset(); }

	void UpdateMaterial();
public:
	struct BufferDescriptor {
		nvrhi::BufferHandle m_Buffer = nullptr;
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

	enum class Type : uint8_t
	{
		Base,
		Default,
		Skinned,
		Dynamic,
		SubIndex
	};

	enum class Flags : uint8_t
	{
		None = 0,
		LandLOD4 = 1 << 0,
		DismemberSkinInstance = 1 << 1,
		Eyes = 1 << 2
	};

	virtual ~BaseMesh() = default;

	// Constructs the appropriate mesh type (DirectMesh, SkinnedMesh, DismemberMesh or DynamicMesh) for the given geometry.
	static eastl::unique_ptr<BaseMesh> Create(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	// Returns true if the hidden state changed (which flags the mesh structurally dirty).
	virtual void SetHidden(bool hidden);

	bool IsHidden() const;

	virtual void OnDestroy();

	virtual SkinnedMesh* AsSkinnedMesh() { return nullptr; }

	virtual DynamicMesh* AsDynamicMesh() { return nullptr; }

	virtual class SubIndexMesh* AsSubIndexMesh() { return nullptr; }

	// Bindless slot of the live (skinned) dynamic float4 position buffer; 0 for non-dynamic meshes.
	virtual uint32_t GetDynamicIndex() const { return 0; }

	// CPU-side per-frame update: detect whether the mesh's geometry data changed (lazy),
	// sync cluster transforms, and populate any traversal-time data.
	// Returns true if changed (so the owning cluster is flagged for refit). No-op for static meshes.
	virtual void Update(nvrhi::ICommandList* commandList);

	// True for meshes whose vertex data changes per frame, so their cluster BLAS must be refit.
	virtual bool IsUpdatable() const { return false; }

	bool IsTwoSided();

	// Returns the mesh's geometry descs (identity transform with the local-to-owner transform baked in).
	// DirectMesh holds a single desc; SkinnedMesh/DynamicMesh hold one per partition.
	virtual const eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescs() const { return m_GeometryDescs; }

	RE::BSTriShape* GetTriShape() const { return m_BSTriShape; }

	BLASCluster* GetCluster() const { return m_Cluster; }
	void SetCluster(BLASCluster* cluster) { m_Cluster = cluster; }

	const eastl::string& GetName() const { return m_Name; }

	RE::TESObjectREFR* GetOwner() const { return m_Owner; }

	RE::TESObjectREFR* GetPrevOwner() const { return m_PrevOwner; }

	// Stores the owner pointer for grouping/comparison only (never dereferenced); returns true if it changed.
	bool SetOwner(RE::TESObjectREFR* owner);

	// Some eye meshes use EnvironmentMap shader instead of Eye shader
	// Detect them by comparing geometry name to headpart name
	void SetEyeFlag();

	const float3x4& GetTransform() const { return m_Transform; }

	const float3x4& GetPrevTransform() const { return m_PrevTransform; }

	const auto& GetWorldBound() const { return m_WorldBound; }
	
	const Properties& GetProperties() const { return m_Properties; }
	const eastl::shared_ptr<MaterialBase>& GetMaterial() const { return m_Material; }
	
	CESEAdapter::REX::EnumSet<DirtyFlags> GetDirtyFlags() const { return m_DirtyFlags; }

	virtual void UpdateLocalTransform(const float4x4& invTransform, const float4x4& prevInvTransform);

	// Writes one MeshData per geometry into 'out' (starting at out[0]); returns the number written.
	uint32_t WriteMeshData(MeshData* out) const;

	void MarkDirty(DirtyFlags flag);

	uint64_t GetVertexDescRaw() const { return *reinterpret_cast<const uint64_t*>(&m_VertexDesc); }

	void SetLastVisitedFrame(uint64_t f) { m_LastVisitedFrame = f; }
	uint64_t GetLastVisitedFrame() const { return m_LastVisitedFrame; }

	CESEAdapter::REX::EnumSet<Flags> GetFlags() const { return m_Flags; }
	bool HasFlag(Flags f) const { return m_Flags.all(f); }

	// Per-geometry index buffer descriptor index (into the Triangles bindless table).
	virtual uint16_t GetIndexID(size_t geometryIndex) const = 0;

	// Vertex buffer descriptor index (into the Vertices bindless table); shared across the mesh's geometries.
	virtual uint16_t GetVertexID() const = 0;

protected:

	static eastl::string MakeDebugName(RE::BSTriShape* bsTriShape);

	static bool ValidateCounts(uint16_t numTriangles, uint32_t numVertices);

	static BufferDescriptor CreateIndexBuffer(RE::BSGraphics::TriShape* triShape);

	static BufferDescriptor CreateVertexBuffer(RE::BSGraphics::TriShape* triShape);

	static nvrhi::rt::GeometryDesc MakeGeometryDesc(nvrhi::IBuffer* indexBuffer, uint32_t indexOffset, uint32_t indexCount, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, uint32_t vertexCount);

	void CreateMaterial();

	eastl::string m_Name;

	RE::BSTriShape* m_BSTriShape = nullptr;

	RE::TESObjectREFR* m_Owner = nullptr;

	RE::TESObjectREFR* m_PrevOwner = nullptr;

	eastl::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;

	// Back-pointer to the BLAS cluster this mesh belongs to; set by AddMember, used for fast removal.
	BLASCluster* m_Cluster = nullptr;

	// Cached world transform from BSTriShape, refreshed in Update().
	float3x4 m_Transform = Constants::kIdentityTransform;
	float3x4 m_PrevTransform = Constants::kIdentityTransform;
	bool m_NeedsPrevInit = true;

	// Local to the BLASCluster
	float3x4 m_LocalTransform = Constants::kIdentityTransform;
	float3x4 m_PrevLocalTransform = Constants::kIdentityTransform;

	RE::NiBound m_WorldBound;

	mutable uint64_t m_LastVisitedFrame = Constants::INVALID_FRAME_INDEX;

	// Native 64-bit vertex descriptor (RE::BSGraphics::VertexDesc) for MeshData::Flags.
	RE::BSGraphics::VertexDesc m_VertexDesc;

	// Mesh-owned dirty state consumed by the owning BLASCluster (Visibility => rebuild, Vertex/Skin => refit).
	// Transform is tracked for prev-transform/motion bookkeeping only; world motion is carried by the TLAS instance.
	CESEAdapter::REX::EnumSet<DirtyFlags> m_DirtyFlags = DirtyFlags::Visibility;

	CESEAdapter::REX::EnumSet<State> m_State = State::None;

	Type m_Type = Type::Base;
	CESEAdapter::REX::EnumSet<Flags> m_Flags = Flags::None;

	// Prevents BSTriShape being destroyed mid-usage
	std::mutex m_BSTriShapeMutex;

	// Shader and Alpha properties
	Properties m_Properties;

	eastl::shared_ptr<MaterialBase> m_Material;
};
