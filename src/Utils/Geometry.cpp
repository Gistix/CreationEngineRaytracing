#pragma once

#include "Geometry.h"
#include "Core/Mesh.h"

namespace Util
{
	namespace Geometry
	{
		std::uint32_t GetSkyrimVertexSize(RE::BSGraphics::Vertex::Flags flags)
		{
			using RE::BSGraphics::Vertex;

			std::uint32_t vertexSize = 0;

			if (flags & Vertex::VF_VERTEX) {
				vertexSize += sizeof(float) * 4;
			}
			if (flags & Vertex::VF_UV) {
				vertexSize += sizeof(std::uint16_t) * 2;
			}
			if (flags & Vertex::VF_UV_2) {
				vertexSize += sizeof(std::uint16_t) * 2;
			}
			if (flags & Vertex::VF_NORMAL) {
				vertexSize += sizeof(std::uint16_t) * 2;
				if (flags & Vertex::VF_TANGENT) {
					vertexSize += sizeof(std::uint16_t) * 2;
				}
			}
			if (flags & Vertex::VF_COLORS) {
				vertexSize += sizeof(std::uint8_t) * 4;
			}
			if (flags & Vertex::VF_SKINNED) {
				vertexSize += sizeof(std::uint16_t) * 4 + sizeof(std::uint8_t) * 4;
			}
			if (flags & Vertex::VF_EYEDATA) {
				vertexSize += sizeof(float);
			}
			if (flags & Vertex::VF_LANDDATA) {
				vertexSize += sizeof(uint32_t) * 2;
			}

			return vertexSize;
		}

		uint16_t GetStoredVertexSize(uint64_t desc)
		{
			return (desc & 0xF) * 4;
		}

		bool HasDoubleSidedGeom(Mesh* mesh)
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
			seen.reserve(mesh->triangleData.triangles.size());

			for (const Triangle& tri : mesh->triangleData.triangles)
			{
				std::array<int32_t, 3> positions[3] = {
					quantize(mesh->vertexData.vertices[tri.x].Position),
					quantize(mesh->vertexData.vertices[tri.y].Position),
					quantize(mesh->vertexData.vertices[tri.z].Position),
				};

				std::sort(positions, positions + 3, cmp);

				TriangleKey key{ positions[0], positions[1], positions[2] };

				if (!seen.insert(key).second)
					return true;
			}

			return false;
		}
	}
}