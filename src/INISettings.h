#pragma once

struct INISettings
{
	// Display
	bool enableProjecteUVDiffuseNormals;
	bool enableProjecteUVDiffuseNormalsOnCubemap;
	float projectedUVDiffuseNormalTilingScale;
	float projectedUVNormalDetailTilingScale;

	void Initialize();
};