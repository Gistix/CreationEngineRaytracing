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

class SceneGraph;

struct Mesh
{
	enum class Flags : uint8_t
	{
		None = 0,
		Dynamic = 1 << 1,
		Skinned = 1 << 2,
		Landscape = 1 << 3,
		Static = 1 << 4,
		DoubleSidedGeom = 1 << 5
	};

	enum class State : uint8_t
	{
		None = 0,
		Hidden = 1 << 0,
		DismemberHidden = 1 << 1
	};

	eastl::string m_Name;

	uint vertexCount = 0;
	uint triangleCount = 0;
	RE::BSGraphics::Vertex::Flags vertexFlags;

	RE::NiPointer<RE::BSGeometry> bsGeometryPtr;

	struct MeshGeometry
	{
		eastl::vector<float4> dynamicPosition;
		eastl::vector<Vertex> vertices;
		eastl::vector<Skinning> skinning;
		eastl::vector<Triangle> triangles;
	} geometry;

	struct MeshBuffers
	{
		nvrhi::BufferHandle dynamicPositionBuffer;
		nvrhi::BufferHandle vertexBuffer;
		nvrhi::BufferHandle vertexCopyBuffer;
		nvrhi::BufferHandle triangleBuffer;
		nvrhi::BufferHandle skinningBuffer;
	} buffers;

	eastl::vector<float3x4> m_BoneMatrices;

	nvrhi::rt::GeometryDesc geometryDesc;

	Material material;

	stl::enumeration<Flags, uint8_t> flags = Flags::None;

	float3x4 localToRoot;

	// DismemberSkinInstance slot
	uint16_t slot;

	uint32_t m_FrameID;

	DescriptorHandle m_DescriptorHandle;

	Mesh(Flags flags, const char* name, RE::BSGeometry* bsGeometryPtr, float3x4 localToRoot, bool dismemberVisible = true, uint16_t slot = 0) :
		flags(flags), m_Name(name), bsGeometryPtr(bsGeometryPtr), localToRoot(localToRoot), slot(slot)
	{
		UpdateDismember(dismemberVisible);
	}

	void BuildMesh(RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint32_t& triangleCountIn, const uint16_t& bonesPerVertex);

	void CalculateNormals();

	Texture GetTexture(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, bool modelSpaceNormalMap);

	void BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, RE::FormID formID);

	void CreateBuffers(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList);

	bool UpdateDynamicPosition();

	void UpdateUploadDynamicBuffers(nvrhi::ICommandList* commandList);

	bool UpdateSkinning();

	DirtyFlags Update();

	bool IsHidden() const;

	bool IsPendingHidden() const;

	bool IsDirtyState() const;

	MeshData GetData(float3 externalEmittance) const;
private:
	// State is pending until BLASRebuild
	State pendingState = State::None;
	State state = State::None;

	void SetPendingState(State stateIn, bool activate);

	void UpdateDismember(bool enable);

	void UpdateState();
};

DEFINE_ENUM_FLAG_OPERATORS(Mesh::Flags);
DEFINE_ENUM_FLAG_OPERATORS(Mesh::State);
