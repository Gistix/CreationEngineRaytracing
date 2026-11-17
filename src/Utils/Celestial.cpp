#include "Celestial.h"
#include "Utils/Adapter.h"
#include "Utils/Math.h"

#include "interop/SharedData.hlsli"

namespace Util
{
	namespace Celestial
	{
#if defined(SKYRIM)
		namespace
		{
			// Values from Community Shaders' Util::Moon
			const float3 MasserBaseColor = { 142.0f / 255.0f, 96.0f / 255.0f, 90.0f / 255.0f };
			constexpr float MasserBaseIntensity = 0.5f;
			const float3 SecundaBaseColor = { 117.0f / 255.0f, 115.0f / 255.0f, 109.0f / 255.0f };
			constexpr float SecundaBaseIntensity = 0.25f;

			constexpr float NewMoonIntensityFactor = 0.05f;
			constexpr float CrescentMoonIntensityFactor = 0.25f;
			constexpr float FullMoonIntensityFactor = 1.0f;

			// Elevation over which a moon fades in above the horizon
			constexpr float HorizonFadeRadians = 5.0f * DirectX::XM_PI / 180.0f;

			using Phase = RE::Moon::Phase;

			Phase GetPhaseFromTexture(const char* name)
			{
				if (!name)
					return Phase::kFull;

				static constexpr std::pair<std::string_view, Phase> PhaseLookup[] = {
					{ "full", Phase::kFull },
					{ "three_wan", Phase::kWaningGibbous },
					{ "half_wan", Phase::kWaningQuarter },
					{ "one_wan", Phase::kWaningCrescent },
					{ "new", Phase::kNewMoon },
					{ "one_wax", Phase::kWaxingCrescent },
					{ "half_wax", Phase::kWaxingQuarter },
					{ "three_wax", Phase::kWaxingGibbous },
				};

				std::string lower(name);
				for (auto& c : lower)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

				for (const auto& [key, phase] : PhaseLookup) {
					if (lower.find(key) != std::string::npos)
						return phase;
				}

				return Phase::kFull;
			}

			float GetPhaseIntensityFactor(Phase phase)
			{
				// Enum runs full -> waning -> new -> waxing, so the distance from new is symmetric (0..4).
				const int distance = std::abs(static_cast<int>(phase) - static_cast<int>(Phase::kNewMoon));
				if (distance == 0)
					return NewMoonIntensityFactor;

				const float t = std::clamp((static_cast<float>(distance) - 1.0f) / 3.0f, 0.0f, 1.0f);
				return std::lerp(CrescentMoonIntensityFactor, FullMoonIntensityFactor, t);
			}

			float GetHorizonDim(const float3& dir)
			{
				const float height = std::asin(std::clamp(dir.z, -1.0f, 1.0f));
				return std::clamp(height / HorizonFadeRadians, 0.0f, 1.0f);
			}

			MoonLight EvaluateMoon(RE::Sky* sky, RE::Moon* moon, const float3& baseColor, float baseIntensity)
			{
				MoonLight result;

				if (!moon || !moon->root || !moon->moonNode || !moon->moonMesh)
					return result;

				if (Util::Adapter::IsNiAVObjectHidden(moon->root.get()) || Util::Adapter::IsNiAVObjectHidden(moon->moonNode.get()))
					return result;

				// SkySync: facing axis from the moon's local rotation, brought to world space by the sky root.
				RE::NiPoint3 raw = moon->root->local.rotate.GetVectorY();
				RE::NiPoint3 world = sky->root ? sky->root->world.rotate * raw : raw;

				float3 dir = Util::Math::Float3(world);
				if (dir.LengthSquared() <= FLT_EPSILON)
					return result;
				dir.Normalize();

				auto* shaderProperty = netimmerse_cast<RE::BSSkyShaderProperty*>(Util::Adapter::GetGeometryRuntimeData(moon->moonMesh.get()).shaderProperty);
				if (!shaderProperty)
					return result;

				float phase = 1.0f;
				if (auto* texture = shaderProperty->pBaseTexture)
					phase = GetPhaseIntensityFactor(GetPhaseFromTexture(texture->name.c_str()));

				const float alpha = shaderProperty->kBlendColor.alpha;

				result.Direction = dir;
				result.Color = baseColor;
				result.Intensity = std::max(baseIntensity * phase * alpha * GetHorizonDim(dir), 0.0f);

				return result;
			}
		}
#endif

		bool GetNightLights(NightLights& out)
		{
			out = {};

#if defined(SKYRIM)
			auto* sky = RE::Sky::GetSingleton();
			if (!sky || !sky->root || sky->mode.get() != RE::Sky::Mode::kFull)
				return false;

			if (sky->sun && sky->sun->root) {
				const auto sunOffset = sky->sun->root->world.translate - sky->root->world.translate;
				out.SunBelowHorizon = sunOffset.z < 0.0f;
			}

			if (!out.SunBelowHorizon)
				return true;

			out.Masser = EvaluateMoon(sky, sky->masser, MasserBaseColor, MasserBaseIntensity);
			out.Secunda = EvaluateMoon(sky, sky->secunda, SecundaBaseColor, SecundaBaseIntensity);

			return true;
#else
			return false;
#endif
		}

		namespace
		{
			using Mat3 = std::array<std::array<float, 3>, 3>;

			// Matrices from shaders/include/Common/ColorSpaces.hlsli
			constexpr Mat3 sRGB_2_XYZ = { { { 0.4123907993f, 0.3575843394f, 0.1804807884f },
				{ 0.2126390059f, 0.7151686788f, 0.0721923154f },
				{ 0.0193308187f, 0.1191947798f, 0.9505321522f } } };

			constexpr Mat3 D65_2_D60 = { { { 1.0130349146f, 0.0061052578f, -0.0149709436f },
				{ 0.0076982301f, 0.9981633521f, -0.0050320385f },
				{ -0.0028413174f, 0.0046851567f, 0.9245061375f } } };

			constexpr Mat3 XYZ_2_AP1 = { { { 1.6410233797f, -0.3248032942f, -0.2364246952f },
				{ -0.6636628587f, 1.6153315917f, 0.0167563477f },
				{ 0.0117218943f, -0.0082844420f, 0.9883948585f } } };

			float3 Mul(const Mat3& m, const float3& v)
			{
				return float3(
					m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
					m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
					m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
			}

			// Matches TransferFunctions::SRGBToLinear
			float SRGBToLinear(float encoded)
			{
				const float magnitude = std::abs(encoded);
				const float linear = magnitude <= 0.04045f ? magnitude / 12.92f : std::pow((magnitude + 0.055f) / 1.055f, 2.4f);
				return std::copysign(linear, encoded);
			}

			bool UsesACEScg(const LinearLightingSettings& ll)
			{
				return ll.enableLinearLighting && ll.enableACEScg;
			}
		}

		float3 ToLightInputSpace(const float3& srgbEncoded, const LinearLightingSettings& ll)
		{
			// Linear lighting off: DirLightToLinear applies the sRGB transfer itself
			if (!ll.enableLinearLighting)
				return srgbEncoded;

			float3 linear(SRGBToLinear(srgbEncoded.x), SRGBToLinear(srgbEncoded.y), SRGBToLinear(srgbEncoded.z));

			if (UsesACEScg(ll))
				linear = Mul(XYZ_2_AP1, Mul(D65_2_D60, Mul(sRGB_2_XYZ, linear)));

			return linear;
		}

		float WorkingLuminance(const float3& color, const LinearLightingSettings& ll)
		{
			// Matches Color::RGBToLuminance in shaders/include/Common/Color.hlsli
			if (UsesACEScg(ll))
				return color.Dot(float3(0.2722287168f, 0.6740817658f, 0.0536895174f));
			return color.Dot(float3(0.2125f, 0.7154f, 0.0721f));
		}
	}
}
