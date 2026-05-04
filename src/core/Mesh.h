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
		Origin = 1 << 8,
		LOD = 1 << 9
	};

	enum class State : uint8_t
	{
		None = 0,
		Hidden = 1 << 0,
		DismemberHidden = 1 << 1,
		SubIndexHidden = 1 << 2
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

	static VertexData BuildVertices(stl::enumeration<Flags>& flags, RE::BSGeometry* geometry, RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint16_t& bonesPerVertex);
	static TriangleData BuildTriangles(Mesh::Flags flags, RE::BSGraphics::TriShape* rendererData, const uint32_t& triangleCountIn);
	void BuildMesh(RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint32_t& triangleCountIn, const uint16_t& bonesPerVertex);
	void BuildMesh(VertexData a_VertexData, TriangleData a_TriangleData, RE::BSGraphics::VertexDesc vertexDesc);
	void ClearUnusedVertices();

	void CalculateNormals();

	Texture GetTexture(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, TextureType textureType = TextureType::Standard);

	void BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, RE::TESForm* form);

	void CreateBuffers(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList);

	bool UpdateDynamicPosition();

	void UpdateUploadDynamicBuffers(nvrhi::ICommandList* commandList);

	bool UpdateSkinning(bool isPlayer);

	bool UpdateTransform(RE::NiAVObject* object);

	void UpdateDismember();

	void UpdateSubIndex();

	DirtyFlags Update(RE::NiAVObject* instanceRoot, bool isPlayer, Flags modelFlags);

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
