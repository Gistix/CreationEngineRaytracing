#pragma once

struct LinearLightingSettings;

namespace Util
{
	namespace Celestial
	{
		struct MoonLight
		{
			float3 Direction = { 0.0f, 0.0f, 1.0f };  // Unit vector from the scene towards the moon
			float3 Color = { 0.0f, 0.0f, 0.0f };      // Base colour of the moon
			float Intensity = 0.0f;                   // Base intensity * phase * visibility * horizon fade

			bool IsLit() const { return Intensity > 0.0f; }
		};

		struct NightLights
		{
			bool SunBelowHorizon = false;
			MoonLight Masser;
			MoonLight Secunda;
		};

		// Evaluates sun/moon state from RE::Sky, following Community Shaders' SkySync moon model.
		// Returns false when the sky is unavailable (interiors, menus).
		bool GetNightLights(NightLights& out);

		// Converts an sRGB-encoded colour (e.g. MoonLight::Color) to the space DirLightToLinear expects for Light.Color:
		// sRGB-encoded with linear lighting off, linear working space (sRGB or ACEScg/AP1) with it on.
		float3 ToLightInputSpace(const float3& srgbEncoded, const LinearLightingSettings& ll);

		// Luminance of a colour in the space returned by ToLightInputSpace.
		float WorkingLuminance(const float3& color, const LinearLightingSettings& ll);
	}
}
