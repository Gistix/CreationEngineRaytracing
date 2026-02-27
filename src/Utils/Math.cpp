#include "Math.h"

namespace Util
{
	namespace Math
	{
		uint2 GetDispatchCount(uint2 resolution, float threads)
		{
			uint dispatchX = static_cast<uint>(std::ceil(resolution.x / threads));
			uint dispatchY = static_cast<uint>(std::ceil(resolution.y / threads));

			return { dispatchX, dispatchY };
		}

		uint32_t DivideRoundUp(uint32_t x, uint32_t divisor)
		{
			return (x + divisor - 1) / divisor;
		}

		uint32_t DivideRoundUp(uint32_t x, float divisor)
		{
			return static_cast<uint32_t>(ceil(x / divisor));
		}


		float3 Float3(RE::NiPoint3 niPoint)
		{
			return float3(niPoint.x, niPoint.y, niPoint.z);
		}

		float3 Normalize(float3 vector)
		{
			vector.Normalize();
			return vector;
		}

		DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform)
		{
			DirectX::XMMATRIX temp;

			const RE::NiMatrix3& m = Transform.rotate;
			const float scale = Transform.scale;

			temp.r[0] = DirectX::XMVectorScale(DirectX::XMVectorSet(
				m.entry[0][0],
				m.entry[1][0],
				m.entry[2][0],
				0.0f),
				scale);

			temp.r[1] = DirectX::XMVectorScale(DirectX::XMVectorSet(
				m.entry[0][1],
				m.entry[1][1],
				m.entry[2][1],
				0.0f),
				scale);

			temp.r[2] = DirectX::XMVectorScale(DirectX::XMVectorSet(
				m.entry[0][2],
				m.entry[1][2],
				m.entry[2][2],
				0.0f),
				scale);

			temp.r[3] = DirectX::XMVectorSet(
				Transform.translate.x,
				Transform.translate.y,
				Transform.translate.z,
				1.0f);

			return temp;
		}
	}
}