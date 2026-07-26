#include "INISettings.h"

void INISettings::Initialize()
{
	// Display
	enableProjecteUVDiffuseNormals = Util::Adapter::GetINISettingBool("bEnableProjecteUVDiffuseNormals:Display");
	enableProjecteUVDiffuseNormalsOnCubemap = Util::Adapter::GetINISettingBool("bEnableProjecteUVDiffuseNormalsOnCubemap:Display");
	projectedUVDiffuseNormalTilingScale = RE::GetINISetting("fProjectedUVDiffuseNormalTilingScale:Display")->GetFloat();
	projectedUVNormalDetailTilingScale = RE::GetINISetting("fProjectedUVNormalDetailTilingScale:Display")->GetFloat();
}