#pragma once

#include <PCH.h>

#include "Vertex.hlsli"
#include "Triangle.hlsli"
#include "Skinning.hlsli"

struct Geometry
{
	eastl::vector<float4> dynamicPosition;
	eastl::vector<Vertex> vertices;
	eastl::vector<Skinning> skinning;
	eastl::vector<Triangle> triangles;
};