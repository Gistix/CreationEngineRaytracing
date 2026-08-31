#pragma once

#include "Constants.h"
#include <cmath>
#include <algorithm>
#include <cfloat>

struct AABB
{
	float3 Min{ FLT_MAX, FLT_MAX, FLT_MAX };
	float3 Max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

	AABB() = default;
	AABB(const float3& a_min, const float3& a_max) : Min(a_min), Max(a_max) {}

	bool IsValid() const
	{
		return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z;
	}

	float3 GetCenter() const
	{
		return (Min + Max) * 0.5f;
	}

	float3 GetExtents() const
	{
		return Max - Min;
	}

	float3 GetHalfExtents() const
	{
		return (Max - Min) * 0.5f;
	}

	float GetVolume() const
	{
		if (!IsValid())
			return 0.0f;
		const float3 ext = GetExtents();
		return ext.x * ext.y * ext.z;
	}

	float GetSurfaceArea() const
	{
		if (!IsValid())
			return 0.0f;
		const float3 ext = GetExtents();
		return 2.0f * (ext.x * ext.y + ext.y * ext.z + ext.z * ext.x);
	}

	void Expand(const float3& point)
	{
		Min.x = std::min(Min.x, point.x);
		Min.y = std::min(Min.y, point.y);
		Min.z = std::min(Min.z, point.z);
		Max.x = std::max(Max.x, point.x);
		Max.y = std::max(Max.y, point.y);
		Max.z = std::max(Max.z, point.z);
	}

	void Union(const AABB& other)
	{
		if (!other.IsValid())
			return;
		Min.x = std::min(Min.x, other.Min.x);
		Min.y = std::min(Min.y, other.Min.y);
		Min.z = std::min(Min.z, other.Min.z);
		Max.x = std::max(Max.x, other.Max.x);
		Max.y = std::max(Max.y, other.Max.y);
		Max.z = std::max(Max.z, other.Max.z);
	}

	bool Intersects(const AABB& other) const
	{
		return (Min.x <= other.Max.x && Max.x >= other.Min.x) &&
		       (Min.y <= other.Max.y && Max.y >= other.Min.y) &&
		       (Min.z <= other.Max.z && Max.z >= other.Min.z);
	}

	AABB Intersection(const AABB& other) const
	{
		if (!Intersects(other))
			return AABB();

		return AABB(
			float3(std::max(Min.x, other.Min.x), std::max(Min.y, other.Min.y), std::max(Min.z, other.Min.z)),
			float3(std::min(Max.x, other.Max.x), std::min(Max.y, other.Max.y), std::min(Max.z, other.Max.z))
		);
	}

	float OverlapRatio(const AABB& other) const
	{
		const float vA = GetVolume();
		const float vB = other.GetVolume();
		const float minV = std::min(vA, vB);
		if (minV <= 0.0f)
			return 0.0f;

		const AABB inter = Intersection(other);
		return inter.GetVolume() / minV;
	}

	// Exact O(1) transformation of AABB by a row-major 3x4 affine matrix
	AABB Transform(const float3x4& transform) const
	{
		if (!IsValid())
			return *this;

		const float3 center = GetCenter();
		const float3 halfExt = GetHalfExtents();

		// World center: R * center + T
		const float worldCenterX = transform._11 * center.x + transform._12 * center.y + transform._13 * center.z + transform._14;
		const float worldCenterY = transform._21 * center.x + transform._22 * center.y + transform._23 * center.z + transform._24;
		const float worldCenterZ = transform._31 * center.x + transform._32 * center.y + transform._33 * center.z + transform._34;

		// World half extents: |R| * halfExt
		const float worldExtX = std::abs(transform._11) * halfExt.x + std::abs(transform._12) * halfExt.y + std::abs(transform._13) * halfExt.z;
		const float worldExtY = std::abs(transform._21) * halfExt.x + std::abs(transform._22) * halfExt.y + std::abs(transform._23) * halfExt.z;
		const float worldExtZ = std::abs(transform._31) * halfExt.x + std::abs(transform._32) * halfExt.y + std::abs(transform._33) * halfExt.z;

		const float3 worldCenter(worldCenterX, worldCenterY, worldCenterZ);
		const float3 worldHalfExt(worldExtX, worldExtY, worldExtZ);

		return AABB(worldCenter - worldHalfExt, worldCenter + worldHalfExt);
	}
};
