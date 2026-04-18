#pragma once

namespace Util
{
	namespace Game
	{
		float4 GetClippingData();

		bool IsGlobalLight(RE::BSLight* a_light);

		bool IsHidden(RE::NiAVObject* object, RE::NiAVObject* root = nullptr);
	}

	namespace Units
	{
		// Conversion constants
		constexpr float GAME_UNIT_TO_CM = 1.428f;
		constexpr float GAME_UNIT_TO_M = GAME_UNIT_TO_CM / 100.0f;
		constexpr float GAME_UNIT_TO_FEET = GAME_UNIT_TO_CM / 30.48f;
		constexpr float GAME_UNIT_TO_INCHES = GAME_UNIT_TO_CM / 2.54f;

		// Wind speed conversions
		constexpr float WIND_RAW_TO_NORMALIZED = 1.0f / 255.0f;  // Raw to 0-1 scale
		constexpr float WIND_RAW_TO_PERCENT = 100.0f / 255.0f;   // Raw to percentage

		// Direction conversions
		constexpr float DIR_RAW_TO_DEGREES = 360.0f / 256.0f;    // Raw 0-256 to 0-360 degrees
		constexpr float DIR_RANGE_TO_DEGREES = 180.0f / 256.0f;  // Range 0-256 to 0-180 degrees
		constexpr float RADIANS_TO_DEGREES = 180.0f / DirectX::XM_PI;

		// Distance conversions
		inline float GameUnitsToMeters(float gameUnits) { return gameUnits * GAME_UNIT_TO_M; }
		inline float GameUnitsToCm(float gameUnits) { return gameUnits * GAME_UNIT_TO_CM; }
		inline float GameUnitsToFeet(float gameUnits) { return gameUnits * GAME_UNIT_TO_FEET; }
		inline float GameUnitsToInches(float gameUnits) { return gameUnits * GAME_UNIT_TO_INCHES; }
	}
}