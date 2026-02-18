#pragma once

#include <PCH.h>

#include "Types.h"
#include "Geometry.h"

struct Shape
{
	enum class Flags : uint8_t
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

	RE::BSGeometry* geometry = nullptr;

	Geometry geometry;

	stl::enumeration<Flags, uint8_t> flags = Flags::None;

	void BuildMesh(RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint32_t& triangleCountIn, const uint16_t& bonesPerVertex);

	void CalculateVectors(bool calculateNormal);

	void CreateBuffers(const std::string& name);

	UpdateFlags Update();
};

DEFINE_ENUM_FLAG_OPERATORS(Shape::Flags);
DEFINE_ENUM_FLAG_OPERATORS(Shape::UpdateFlags);
DEFINE_ENUM_FLAG_OPERATORS(Shape::State);