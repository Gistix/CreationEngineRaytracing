#include "Culling.h"

namespace Util
{
	namespace Culling
	{
		bool ShouldCull(RE::BSGeometry& geometry)
		{
			static const REL::Relocation<const RE::NiRTTI*> skyRTTI{ RE::BSSkyShaderProperty::Ni_RTTI };
			static const REL::Relocation<const RE::NiRTTI*> particleRTTI{ RE::BSParticleShaderProperty::Ni_RTTI };

			auto* shaderPropertyRTTI = geometry.GetGeometryRuntimeData().shaderProperty->GetRTTI();

			if (shaderPropertyRTTI == skyRTTI.get())
				return false;

			if (shaderPropertyRTTI == particleRTTI.get())
				return false;

			return true;
		}
	}
}