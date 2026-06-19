#pragma once

namespace Util
{
	namespace Math
	{
		uint2 GetDispatchCount(uint2 resolution, float threads);

		uint32_t DivideRoundUp(uint32_t x, uint32_t divisor);

		uint32_t DivideRoundUp(uint32_t x, float divisor);

		float3 Float3(RE::NiPoint3 niPoint);

		float3 Float3(RE::NiColor niColor);

		float4 Float4(RE::NiColorA niColor);

		float3 Normalize(float3 vector);

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