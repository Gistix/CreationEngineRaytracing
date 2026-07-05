#include "Culling.h"
#include "Adapter.h"

namespace Util
{
	namespace Culling
	{
		bool ShouldCull(RE::BSGeometry* geometry)
		{
			static const REL::Relocation<const RE::NiRTTI*> skyRTTI{ NiRTTI(BSSkyShaderProperty) };
#if defined(SKYRIM)
			static const REL::Relocation<const RE::NiRTTI*> particleRTTI{ NiRTTI(BSParticleShaderProperty) };
#endif

			auto runtimeData = Util::Adapter::GetGeometryRuntimeData(geometry);
			auto* shaderPropertyRTTI = runtimeData.shaderProperty->GetRTTI();

			if (shaderPropertyRTTI == skyRTTI.get())
				return false;

#if defined(SKYRIM)
			if (shaderPropertyRTTI == particleRTTI.get())
				return false;
#endif

			return true;
		}
	}
}