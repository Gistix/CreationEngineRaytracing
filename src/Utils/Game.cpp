#include "Game.h"

namespace Util
{
	namespace Game
	{
		float4 GetClippingData()
		{
#if defined(SKYRIM)
			static float& cameraNear = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x40));
			static float& cameraFar = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x44));
#elif defined(FALLOUT4)

#endif

			float4 cameraData{};
			cameraData.x = cameraFar;
			cameraData.y = cameraNear;
			cameraData.z = cameraFar - cameraNear;
			cameraData.w = cameraFar * cameraNear;

			return cameraData;
		}
	}
}