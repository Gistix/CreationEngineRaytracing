#pragma once

struct INISettings
{
	// Display
	bool enableProjecteUVDiffuseNormals;
	bool enableProjecteUVDiffuseNormalsOnCubemap;
	float projectedUVDiffuseNormalTilingScale;
	float projectedUVNormalDetailTilingScale;
	float leafAnimDampenDistStart;
	float leafAnimDampenDistEnd;

	// Grass wind strength = min(lerp(min, max, sky wind speed), 60) * multiplier
	float grassWindMagnitudeMin;
	float grassWindMagnitudeMax;
	float windGrassMultiplier;
	// Grass is faded out (and its wind skipped) beyond start + range
	float grassStartFadeDistance;
	float grassFadeRange;

	void Initialize();
};