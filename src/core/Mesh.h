#pragma once

#include <PCH.h>

#include "Types.h"
#include "Vertex.hlsli"
#include "Triangle.hlsli"
#include "Skinning.hlsli"

#include "Material.h"

#include "Framework/DescriptorTableManager.h"

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

	enum class UpdateFlags : uint8_t
	{
		None = 0,
		Vertices = 1 << 0,
		Skinning = 1 << 1
	};

	enum class State : uint8_t
	{
		None = 0,
		Hidden = 1 << 0,
		DismemberHidden = 1 << 1
	};

	uint vertexCount = 0;
	uint triangleCount = 0;
	RE::BSGraphics::Vertex::Flags vertexFlags;

	RE::BSGeometry* bsGeometryPtr = nullptr;

	struct MeshGeometry
	{
		eastl::vector<float4> dynamicPosition;
		eastl::vector<Vertex> vertices;
		eastl::vector<Skinning> skinning;
		eastl::vector<Triangle> triangles;
	} geometry;

	struct MeshBuffers
	{
		nvrhi::BufferHandle vertexBuffer;
		nvrhi::BufferHandle triangleBuffer;
		std::shared_ptr<DescriptorHandle> vertexBufferDescriptor;
		std::shared_ptr<DescriptorHandle> triangleBufferDescriptor;
	} buffers;

	nvrhi::rt::GeometryDesc geometryDesc;

	Material material;

	stl::enumeration<Flags, uint8_t> flags = Flags::None;

	float3x4 localToRoot;

	// DismemberSkinInstance slot
	uint16_t slot;

	Mesh(Flags flags, RE::BSGeometry* bsGeometryPtr, float3x4 localToRoot, bool dismemberVisible = true, uint16_t slot = 0) :
		flags(flags), bsGeometryPtr(bsGeometryPtr), localToRoot(localToRoot), slot(slot)
	{
		UpdateDismember(dismemberVisible);
	}


	void BuildMesh(RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint32_t& triangleCountIn, const uint16_t& bonesPerVertex);

	void CalculateVectors(bool calculateNormal);

	void BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, const char* name, RE::FormID formID);

	void CreateBuffers(nvrhi::ICommandList* commandList, const std::string& name);

	UpdateFlags Update();

private:
	// State is pending until BLASRebuild
	State pendingState = State::None;
	State state = State::None;

	void SetPendingState(State stateIn, bool activate);

	void UpdateDismember(bool enable);

	void UpdateState();
};

DEFINE_ENUM_FLAG_OPERATORS(Mesh::Flags);
DEFINE_ENUM_FLAG_OPERATORS(Mesh::UpdateFlags);
DEFINE_ENUM_FLAG_OPERATORS(Mesh::State);