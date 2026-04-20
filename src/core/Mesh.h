#pragma once

#include <PCH.h>

#include "Types.h"
#include "Vertex.hlsli"
#include "Triangle.hlsli"
#include "Skinning.hlsli"

#include "Material.h"

#include "Framework/DescriptorTableManager.h"

#include "Mesh.hlsli"

#include "DirtyFlags.h"

#include <eastl/unordered_set.h>

#include "Core/TextureManager.h"

class SceneGraph;

struct Mesh
{
	enum class Flags : uint16_t
	{
		None = 0,
		Dynamic = 1 << 1,
		Skinned = 1 << 2,
		Landscape = 1 << 3,
		Static = 1 << 4,
		DoubleSidedGeom = 1 << 5,
		Water = 1 << 6,
		Remapped = 1 << 7,
		Origin = 1 << 8
	};

	enum class State : uint8_t
	{
		None = 0,
		Hidden = 1 << 0,
		DismemberHidden = 1 << 1
	};

	eastl::string m_Name;

	RE::BSGraphics::Vertex::Flags vertexFlags;

	RE::NiPointer<RE::BSGeometry> bsGeometryPtr;

	struct VertexData
	{
		uint count = 0;
		eastl::vector<float4> dynamicPosition;
		eastl::vector<Vertex> vertices;
		eastl::vector<Skinning> skinning;
		eastl::vector<uint16_t> remap;
		eastl::vector<float4> dynamicPositionRemapped;
	} vertexData;

	struct TriangleData
	{
		uint count = 0;
		eastl::vector<Triangle> triangles;
	} triangleData;

	struct MeshBuffers
	{
		nvrhi::BufferHandle dynamicPositionBuffer;
		nvrhi::BufferHandle vertexBuffer;
		nvrhi::BufferHandle vertexCopyBuffer;
		nvrhi::BufferHandle prevPositionBuffer;
		nvrhi::BufferHandle triangleBuffer;
		nvrhi::BufferHandle skinningBuffer;
	} buffers;

	eastl::vector<float3x4> m_BoneMatrices;

	nvrhi::rt::GeometryDesc geometryDesc;

	Material material;

	stl::enumeration<Flags> flags = Flags::None;

	float3x4 m_LocalToRoot;
	float3x4 m_PrevLocalToRoot;

	// DismemberSkinInstance slot
	uint8_t m_Partition;

	uint32_t m_FrameID;

	DescriptorHandle m_DescriptorHandle;

	RE::FormType m_FormType;

	Mesh(RE::FormType formType, Flags flags, const char* name, RE::BSGeometry* bsGeometryPtr, float3x4 localToRoot, uint8_t partition = 0) :
		m_FormType(formType), flags(flags), m_Name(name), bsGeometryPtr(bsGeometryPtr), m_LocalToRoot(localToRoot), m_PrevLocalToRoot(localToRoot), m_Partition(partition) { }

	bool HasDoubleSidedGeom()
	{
		static constexpr float kQuantize = 1e2f;

		auto quantize = [](const float3& v) -> std::array<int32_t, 3> {
			return {
				static_cast<int32_t>(std::roundf(v.x * kQuantize)),
				static_cast<int32_t>(std::roundf(v.y * kQuantize)),
				static_cast<int32_t>(std::roundf(v.z * kQuantize)),
			};
		};

		auto cmp = [](const std::array<int32_t, 3>& a, const std::array<int32_t, 3>& b) {
			return std::tie(a[0], a[1], a[2]) < std::tie(b[0], b[1], b[2]);
		};

		struct TriangleKey
		{
			std::array<int32_t, 3> v0, v1, v2;

			bool operator==(const TriangleKey& other) const
			{
				return memcmp(this, &other, sizeof(TriangleKey)) == 0;
			}
		};

		struct TriangleKeyHash
		{
			size_t operator()(const TriangleKey& k) const
			{
				auto hashInt3 = [](const std::array<int32_t, 3>& v) -> size_t {
					size_t h = 0;
					h ^= std::hash<int32_t>{}(v[0]) + 0x9e3779b9 + (h << 6) + (h >> 2);
					h ^= std::hash<int32_t>{}(v[1]) + 0x9e3779b9 + (h << 6) + (h >> 2);
					h ^= std::hash<int32_t>{}(v[2]) + 0x9e3779b9 + (h << 6) + (h >> 2);
					return h;
					};
				size_t h = 0;
				h ^= hashInt3(k.v0) + 0x9e3779b9 + (h << 6) + (h >> 2);
				h ^= hashInt3(k.v1) + 0x9e3779b9 + (h << 6) + (h >> 2);
				h ^= hashInt3(k.v2) + 0x9e3779b9 + (h << 6) + (h >> 2);
				return h;
			}
		};

		eastl::hash_set<TriangleKey, TriangleKeyHash> seen;
		seen.reserve(triangleData.triangles.size());

		for (const Triangle& tri : triangleData.triangles)
		{
			std::array<int32_t, 3> positions[3] = {
				quantize(vertexData.vertices[tri.x].Position),
				quantize(vertexData.vertices[tri.y].Position),
				quantize(vertexData.vertices[tri.z].Position),
			};

			std::sort(positions, positions + 3, cmp);

			TriangleKey key{ positions[0], positions[1], positions[2] };

			if (!seen.insert(key).second)
				return true;
		}

		return false;
	}

	void BuildVertices(RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint16_t& bonesPerVertex);
	void BuildTriangles(RE::BSGraphics::TriShape* rendererData, const uint32_t& triangleCountIn);
	void BuildMesh(RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint32_t& triangleCountIn, const uint16_t& bonesPerVertex);

	void ClearUnusedVertices();

	void CalculateNormals();

	Texture GetTexture(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, TextureType textureType = TextureType::Standard);

	void BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, RE::TESForm* form);

	void CreateBuffers(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList);

	bool UpdateDynamicPosition();

	void UpdateUploadDynamicBuffers(nvrhi::ICommandList* commandList);

	bool UpdateSkinning(RE::NiAVObject* object, bool isPlayer);

	bool UpdateTransform(RE::NiAVObject* object);

	void UpdateDismember();

	DirtyFlags Update(RE::NiAVObject* instanceRoot, bool isPlayer);

	bool IsHidden() const;

	MeshData GetData(const float3 externalEmittance);

	static eastl::vector<Triangle> GetLandscapeTriangles();
private:
	// State is pending until BLASRebuild
	stl::enumeration<State> m_PendingState = State::None;
	stl::enumeration<State> m_State = State::None;

	void UpdateState();
};

DEFINE_ENUM_FLAG_OPERATORS(Mesh::Flags);
DEFINE_ENUM_FLAG_OPERATORS(Mesh::State);
