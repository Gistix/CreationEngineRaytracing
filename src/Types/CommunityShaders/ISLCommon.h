#pragma once

#include "Types/CommunityShaders/LightLimitFix.h"

struct ISLCommon
{
	static constexpr float Scale = 0.8f;
	static constexpr float MetresToUnits = 70.f;
	static constexpr float MetresToUnitsSq = MetresToUnits * MetresToUnits;
	static constexpr float ScaledUnitsSq = Scale * MetresToUnitsSq;
	static constexpr float FadeZoneBase = 4.5f * Scale * MetresToUnits;

	enum class TES_LIGHT_FLAGS_EXT
	{
		kInverseSquare = 1 << 14,
		kLinear = 1 << 15
	};

	struct RuntimeLightDataExt
	{
		stl::enumeration<LightLimitFix::LightFlags> flags;
		float cutoffOverride;
		RE::FormID lighFormId;
		RE::NiColor diffuse;
		float radius;
		float pad1C;
		float size;
		float fade;
		std::uint32_t unk138;

		static RuntimeLightDataExt* Get(RE::NiLight* niLight)
		{
			return reinterpret_cast<RuntimeLightDataExt*>(&niLight->GetLightRuntimeData());
		}
	};
};