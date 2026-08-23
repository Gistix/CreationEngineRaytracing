#pragma once

#include "Interop/BoneTransform.hlsli"

namespace Util
{
	namespace Math
	{
		uint2 GetDispatchCount(uint2 resolution, float threads);

		uint32_t DivideRoundUp(uint32_t x, uint32_t divisor);

		uint32_t DivideRoundUp(uint32_t x, float divisor);

		float2 Float2(RE::NiPoint2 niPoint);

		float3 Float3(RE::NiPoint3 niPoint);

		float3 Float3(RE::NiColor niColor);

		float4 Float4(RE::NiColorA niColor);

		float3 Normalize(float3 vector);

		inline float3 GetMatrixRow(const RE::NiMatrix3& m, size_t row)
		{
#if defined(SKYRIM)
			return float3(m.entry[row][0], m.entry[row][1], m.entry[row][2]);
#elif defined(FALLOUT4)
			return float3(m.entry[0][row], m.entry[1][row], m.entry[2][row]);
#endif
		}

		inline float3 GetMatrixColumn(const RE::NiMatrix3& m, size_t col)
		{
#if defined(SKYRIM)
			return float3(m.entry[0][col], m.entry[1][col], m.entry[2][col]);
#elif defined(FALLOUT4)
			return float3(m.entry[col][0], m.entry[col][1], m.entry[col][2]);
#endif
		}

		inline void PackNiTransform(const RE::NiTransform& src, float4& r0s, float4& r1, float4& r2, float4& t)
		{
			const float3 row0 = GetMatrixRow(src.rotate, 0);
			const float3 row1 = GetMatrixRow(src.rotate, 1);
			const float3 row2 = GetMatrixRow(src.rotate, 2);

			r0s = float4(row0.x, row0.y, row0.z, src.scale);
			r1  = float4(row1.x, row1.y, row1.z, 0.0f);
			r2  = float4(row2.x, row2.y, row2.z, 0.0f);
			t   = float4(src.translate.x, src.translate.y, src.translate.z, 0.0f);
		}

		inline void PackNiTransform(const RE::NiTransform& src, NiTransformPacked& dst)
		{
			PackNiTransform(src, dst.Rot0_Scale, dst.Rot1, dst.Rot2, dst.Translate);
		}

		DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform);

		inline float3x4 ComputeLocalToRoot(const RE::NiTransform& rootWorldInverse, const RE::NiTransform& geometryWorld)
		{
			float3x4 result;
			XMStoreFloat3x4(&result, GetXMFromNiTransform(rootWorldInverse * geometryWorld));
			return result;
		}

		bool MatrixNearEqual(const float3x4& a, const float3x4& b, float epsilon = 1e-5f);

		bool Intersects(const float2& aCenter, const float2& aSize, const float2& bCenter, const float2& bSize);

		uint64_t Align64KB(uint64_t size);
	}
}