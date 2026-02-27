#pragma once

namespace Util
{
	namespace Math
	{
		uint2 GetDispatchCount(uint2 resolution, float threads);

		uint32_t DivideRoundUp(uint32_t x, uint32_t divisor);

		uint32_t DivideRoundUp(uint32_t x, float divisor);

		float3 Float3(RE::NiPoint3 niPoint);

		float3 Normalize(float3 vector);

		DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform);
	}
}