#pragma once

#include <PCH.h>

#include "Types.h"
#include "Vertex.hlsli"
#include "Triangle.hlsli"
#include "Skinning.hlsli"

struct Shape
{
	enum Flags : uint8_t
	{
		None = 0,
		AlphaBlending = 1 << 0,
		AlphaTesting = 1 << 1,
		Dynamic = 1 << 2,
		Skinned = 1 << 3,
		Landscape = 1 << 4,
		Static = 1 << 5,
		DoubleSidedGeom = 1 << 6
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

	RE::BSGeometry* geometry = nullptr;

	eastl::vector<float4> dynamicPosition;
	eastl::vector<Vertex> vertices;
	eastl::vector<Skinning> skinning;
	eastl::vector<Triangle> triangles;
};