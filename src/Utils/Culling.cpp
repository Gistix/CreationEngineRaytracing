#include "Culling.h"

namespace Util
{
	namespace Culling
	{
		bool ShouldCull(RE::BSGeometry& geometry)
		{
			static const REL::Relocation<const RE::NiRTTI*> skyRTTI{ NiRTTI(BSSkyShaderProperty) };
			static const REL::Relocation<const RE::NiRTTI*> particleRTTI{ NiRTTI(BSParticleShaderProperty) };

			auto* shaderPropertyRTTI = geometry.GetGeometryRuntimeData().shaderProperty->GetRTTI();

			if (shaderPropertyRTTI == skyRTTI.get())
				return false;

			if (shaderPropertyRTTI == particleRTTI.get())
				return false;

			return true;
		}
	}
}