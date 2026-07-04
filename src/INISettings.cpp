#include "INISettings.h"

void INISettings::Initialize()
{
	// Display
	enableProjecteUVDiffuseNormals = RE::GetINISetting("bEnableProjecteUVDiffuseNormals:Display")->GetBool();
	enableProjecteUVDiffuseNormalsOnCubemap = RE::GetINISetting("bEnableProjecteUVDiffuseNormalsOnCubemap:Display")->GetBool();
	projectedUVDiffuseNormalTilingScale = RE::GetINISetting("fProjectedUVDiffuseNormalTilingScale:Display")->GetFloat();
	projectedUVNormalDetailTilingScale = RE::GetINISetting("fProjectedUVNormalDetailTilingScale:Display")->GetFloat();
}